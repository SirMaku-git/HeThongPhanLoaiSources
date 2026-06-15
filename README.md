# Hệ Thống Phân Loại Màu Sắc (HeThongPhanLoaiSources)

Hệ thống tự động phân loại đối tượng theo màu sắc dựa trên hai ESP32 với xử lý hình ảnh và điều khiển.

## Tổng Quan Dự Án

- **Mục đích**: Xây dựng băng chuyền tự động phân loại vật thể theo màu sắc (Đỏ/Xanh Dương/Trống) bằng camera
- **Nền tảng**: ESP32 (CAM + Main Controller)
- **Công nghệ chính**: Arduino, Vision Processing (HSV), Web UI, UART Communication
- **Ngôn ngữ**: C++ (100%)
- **Mục đích sử dụng**: Dự án môn Tự động hóa - Học Viện Công Nghệ Bưu Chính Viễn Thông cơ sở tại TP Hồ Chí Minh

---

## Kiến Trúc Hệ Thống

### 2 Điều khiển chính:

**1. ESP32-CAM**
- Chụp ảnh và xử lý hình ảnh
- Phân loại màu sắc: Đỏ, Xanh Dương, Trống
- Cung cấp Web UI / REST API
- Quản lý Wi-Fi (bao gồm chế độ Access Point dự phòng để nhập mật khẩu)
- Gửi kết quả nhận diện qua UART

**2. ESP32 Main**
- Nhận thông báo màu sắc + trạng thái Wi-Fi từ UART
- Điều khiển tốc độ động cơ băng chuyền bằng biến trở B10K
- Điều khiển servo phân loại
- Hiển thị trạng thái trên LCD I2C

---

## Cấu Trúc Thư Mục

```
HeThongPhanLoaiSources/
├── README.md                          # Tài liệu dự án (tập tin này)
├── ESP32_CAM/
│   └── ESP32_CAM.ino                  # Chứa camera, Wi-Fi, nhận diện màu, detect vật, gửi thông tin về ESP32 chính
└── ESP32_main/
    └── ESP32_main.ino                 # Chứa điều khiển motor, servo, LCD, nhận từ ESP32-CAM
```

---

## Thư Viện và Ứng Dụng

- `Arduino.h`,
- `esp_camera.h`, 
- `esp_http_server.h`,
- `img_converters.h`, 
- `ESP32_OV5640_AF.h` [Link](https://github.com/0015/ESP32-OV5640-AF),
- `WiFi.h`,
- `Preferences.h`,
- `Wire.h`,
- `LiquidCrystal_I2C.h`[Link](https://github.com/johnrickman/LiquidCrystal_I2C),
- `ESP32Servo.h` [Link](https://madhephaestus.github.io/ESP32Servo/annotated.html),
- `L298N.h` [Link](https://github.com/AndreaLombardo/L298N)

## FreeRTOS Trong Dự Án

FreeRTOS là lớp hệ điều hành thời gian thực có sẵn bên dưới Arduino Core của ESP32. Trong dự án này, nó không được dùng theo kiểu tự tạo nhiều task riêng, nhưng vẫn là nền giúp hệ thống không bị kẹt khi một phần việc đang bận và giúp các phần việc trên ESP32 chạy ổn định hơn so với kiểu vòng lặp đơn thuần.

Trong dự án này, FreeRTOS giúp chia tải theo hướng thực tế như sau: ESP32-CAM gánh phần nặng về camera, Wi-Fi, web server và xử lý ảnh; ESP32 Main gánh phần điều khiển motor, servo, LCD và đọc biến trở. Nhờ có nền RTOS, các phần việc này không phải chờ nhau theo kiểu một chuỗi xử lý cứng nhắc, nên hệ thống phản hồi ổn định hơn.

Về RAM và CPU, ESP32-CAM là mạch tốn tài nguyên hơn. RAM của nó bị dùng cho buffer ảnh `shared_buf` 160000 byte, framebuffer `fb_count = 1`, chuỗi web UI, trạng thái Wi-Fi và các dữ liệu trung gian khi xử lý ảnh. CPU của nó cũng bận hơn vì phải chụp ảnh, tính màu, phục vụ HTTP và xử lý Wi-Fi. ESP32 Main nhẹ hơn về RAM vì chủ yếu giữ trạng thái điều khiển và hiển thị, còn CPU của nó tập trung cho vòng điều khiển liên tục: đọc B10K, nhận UART, cập nhật LCD, điều motor và servo.

`frame_mutex` là mutex, tức cơ chế khóa tài nguyên dùng chung. Trong code nó được tạo bằng `xSemaphoreCreateMutex()` và dùng với `xSemaphoreTake()` / `xSemaphoreGive()` để chỉ cho một luồng được quyền đọc hoặc ghi vùng ảnh tại một thời điểm. Nói đơn giản, nó ngăn tình trạng vừa chụp frame vừa trả frame hoặc xử lý frame cùng lúc, tránh ảnh lỗi và treo bộ đệm.

Nếu không có FreeRTOS, dự án sẽ phải chạy đơn luồng hơn và không có lớp điều phối nền để giữ các phần việc bám nhịp nhau. Khi camera hoặc Wi-Fi đang bận, phần nhận UART, xử lý cảm biến, cập nhật LCD và điều khiển cơ khí có thể bị trễ rõ rệt. Trên ESP32-CAM, điều này dễ gây cảm giác "đơ" khi web/server/camera chiếm CPU; trên ESP32 Main, phản hồi motor và servo sẽ kém mượt hơn vì mọi thứ phải chờ nhau trong một vòng xử lý tuần tự.

---

## Chức Năng Chính

### A. Xử Lý Hình Ảnh & Phân Loại
- Chụp ảnh, tính RGB trung bình từ ROI 60x60 pixel
- Chuyển đổi RGB → HSV
- Tính "độ sắc nét" của hình ảnh
- **Phân loại:**
  - **TRỐNG** (empty)
  - **VẬT ĐỎ** (red object)
  - **VẬT XANH DƯƠNG** (blue object)
- **Logic phân loại:** Threshold độ khác biệt nền (bg_diff < 40), HSV range cho đỏ & xanh

### B. Kích Hoạt & Hành Vi Thời Gian Thực [DEBUG]
- IR sensor phát hiện vật thể (falling-edge trigger, debounce 1s)
- Hỗ trợ chụp ảnh bằng tay qua endpoint `/capture`

### C. Web API & Dashboard
**Endpoints chính:**
- `/jpg` - Stream JPEG snapshots
- `/color` - Telemetry JSON (RGB, HSV, label, sharpness)
- `/set_roi`, `/led`, `/calibrate`, `/capture` - Runtime controls

**Dashboard Features:** [DEBUG]
- Di chuyển ROI bằng chuột
- Giám sát RGB/HSV/Label/Sharpness/Frame
- Hiệu chỉnh nền 

### D. Dò Wifi Và Kết Nối
- Lưu trữ tối đa 10 mạng Wi-Fi từ Preferences
- Thử kết nối tuần tự
- Nếu thất bại → Chuyển sang chế độ AP: **ESP32-CAM-SETUP**
- **UART State Notifications:**
  - `WIFI:TRY:<ssid>`
  - `WIFI:IP:<ssid>:<ip>`
  - `WIFI:ERR:<ap>:<ip>`

### E. Hành Vi Điều Khiển (Actuation)
- **Motor:** Chạy liên tục theo hướng ngược
- **Servo (khi phát hiện):**
  - Nhận `COLOR:RED` → Quay servo góc Đỏ
  - Nhận `COLOR:BLUE` → Quay servo góc Xanh
  - Nhận `COLOR:EMPTY` → Về vị trí an toàn (home)
- **LCD:** Hiển thị trạng thái Wi-Fi, IP, màu phát hiện

---

## Giao Thức UART

### Định dạng tin nhắn
**Từ ESP32-CAM → ESP32 Main:**
```
COLOR:RED
COLOR:BLUE
COLOR:EMPTY
WIFI:TRY:<ssid>
WIFI:IP:<ssid>:<ip>
WIFI:ERR:<ap>:<ip>
```

**Từ ESP32 Main → ESP32-CAM:**
```
REQ:WIFI
```
---

## Các Vật Liệu

| Nhóm | Thành phần |
| --- | --- |
| Điều khiển | Mạch ESP32-CAM dạng mainboard tích hợp sẵn cổng USB micro để nạp/nguồn; Mạch ESP32 NodeMCU (bản thường 30 chân); Không dùng mạch USB-to-UART FTDI riêng |
| Cảm biến | Cảm biến ảnh OV5640 AF (Auto Focus); Cảm biến hồng ngoại tránh vật cản (IR) |
| Chấp hành | Mạch cầu H L298N Dual Motor Driver; Động cơ DC giảm tốc (5V - 12V); Động cơ Servo MG90S |
| Hiển thị & Chiếu sáng | Màn hình LCD 1602 kèm mạch chuyển đổi I2C; Đèn Flash LED dán sẵn trên ESP32-CAM |
| Nguồn & Kết nối | Nguồn Adapter 12V (Dòng từ 2A trở lên); Dây cắm Dupont (Đực-Cái, Đực-Đực, Cái-Cái); Cáp USB Micro |
| Cơ khí & Điều chỉnh | Khung băng tải cơ khí; Biến trở xoay đơn B10K |

### Ghi Chú Phần Cứng / Nguồn Cấp

- Hệ thống hiện đang dùng 2 nguồn cấp riêng:
  - 1 đường 5V cấp vào ESP32 main bản 30 chân.
  - 1 đường lấy từ nguồn 12V-1A, cấp vào nhánh ESP32-CAM và đồng thời dùng cho L298N.
- Các phần cứng chính đang dùng trong mô hình:
  - ESP32 main bản thường 30 chân.
  - ESP32-CAM.
  - B10K biến trở để tinh chỉnh/tham chiếu theo mạch.
  - L298N để điều khiển động cơ băng chuyền.
  - Servo MG90S để gạt/phân loại vật thể.
---

## Người Phát Triển

- **SirMaku**
- **Trần Quang Huy**
- **Nguyễn Xuân Khoa**

---


## Ghi Chú

> **Lưu ý:** Các tính năng được đánh dấu `[DEBUG]` trong quá trình phát triển sẽ không được thêm vào phiên bản chính thức nộp trong báo cáo.

> **Đây là dự án mang tính thử nghiệm và trong quá trình học, mã nguồn còn lộn xộn và lỗi do thiếu kinh nghiệm trong quá trình viết.**