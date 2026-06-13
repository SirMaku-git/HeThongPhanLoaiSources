# Hệ Thống Phân Loại Màu Sắc (HeThongPhanLoaiSources)

Hệ thống tự động phân loại đối tượng theo màu sắc dựa trên hai microcontroller ESP32 với xử lý hình ảnh và điều khiển cơ khí.

## Tổng Quan Dự Án

- **Mục đích**: Xây dựng băng chuyền tự động phân loại vật thể theo màu sắc (Đỏ/Xanh Dương/Trống) bằng camera và AI
- **Nền tảng**: ESP32 (CAM + Main Controller)
- **Công nghệ chính**: Arduino, Vision Processing (HSV), Web UI, UART Communication
- **Ngôn ngữ**: C++ (100%)

---

## Kiến Trúc Hệ Thống

### 2 Microcontroller chính:

**1. ESP32-CAM (Nút Sensing)**
- Chụp ảnh và xử lý hình ảnh
- Phân loại màu sắc: Đỏ, Xanh Dương, Trống
- Cung cấp Web UI / REST API
- Quản lý Wi-Fi (bao gồm chế độ Access Point dự phòng)
- Gửi kết quả nhận diện qua UART

**2. ESP32 Main (Nút Điều Khiển)**
- Nhận thông báo màu sắc + trạng thái Wi-Fi từ UART
- Điều khiển động cơ băng chuyền
- Điều khiển servo phân loại
- Hiển thị trạng thái trên LCD I2C

---

## Cấu Trúc Thư Mục

```
HeThongPhanLoaiSources/
├── README.md                          # Tài liệu dự án (tập tin này)
├── ESP32_CAM/
│   └── ESP32_CAM.ino                  # Firmware camera, Wi-Fi, classification
└── ESP32_main/
    └── ESP32_main.ino                 # Firmware điều khiển motor, servo, LCD
```

---

## Công Nghệ & Thư Viện

### Framework & Core
- **Arduino Framework** (Arduino.h)
- **FreeRTOS** (Mutex/Semaphore cho frame buffer)

### Camera & HTTP
- `esp_camera.h`, `esp_http_server.h`
- `img_converters.h`, `ESP32_OV5640_AF.h`

### Kết Nối & Lưu Trữ
- `WiFi.h` (Wi-Fi management)
- `Preferences.h` (Persistent storage)

### Điều Khiển Ngoại Vi
- `Wire.h`, `LiquidCrystal_I2C.h` (LCD I2C)
- `ESP32Servo.h` (Servo control)
- `L298N.h` (Motor driver)

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

### B. Kích Hoạt & Hành Vi Thời Gian Thực
- IR sensor phát hiện vật thể (falling-edge trigger, debounce 1s)
- Hỗ trợ chụp ảnh bằng tay qua endpoint `/capture`

### C. Web API & Dashboard
**Endpoints chính:**
- `/jpg` - Stream JPEG snapshots
- `/color` - Telemetry JSON (RGB, HSV, label, sharpness)
- `/set_roi`, `/led`, `/calibrate`, `/capture` - Runtime controls

**Dashboard Features:**
- Di chuyển ROI bằng chuột
- Giám sát RGB/HSV/Label/Sharpness/Frame
- Hiệu chỉnh nền conveyor
- Điều chỉnh đèn LED

### D. Provisioning Wi-Fi & Failover
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

## Hướng Dẫn Sử Dụng

### [*TODO: Thêm hướng dẫn cài đặt phần cứng*]
- [TODO] Danh sách linh kiện cần thiết
- [TODO] Sơ đồ kết nối
- [TODO] Pinout ESP32 CAM & Main

### [*TODO: Thêm hướng dẫn cài đặt firmware*]
- [TODO] Cài đặt Arduino IDE
- [TODO] Cài đặt board & libraries
- [TODO] Upload firmware
- [TODO] Cài đặt Wi-Fi qua Web UI

### [*TODO: Thêm hướng dẫn sử dụng*]
- [TODO] Truy cập Web Dashboard
- [TODO] Hiệu chỉnh ROI & độ sáng
- [TODO] Điều chỉnh tham số phân loại
- [TODO] Xử lý sự cố

---

## Cải Tiến & Tối Ưu Hóa

### [*TODO: Danh sách cực kỳ cần cải thiện*]
- [TODO] Nâng cao độ chính xác phân loại
- [TODO] Tối ưu hóa tốc độ xử lý
- [TODO] Thêm hỗ trợ nhiều màu sắc hơn
- [TODO] Cải thiện giao diện web
- [TODO] Thêm logging & dữ liệu thống kê

---

## Người Phát Triển

[*TODO: Thêm thông tin tác giả*]

---

## Giấy Phép

[*TODO: Chọn giấy phép (MIT, GPL, v.v.)*]

---

## Liên Hệ & Hỗ Trợ

[*TODO: Thêm thông tin liên hệ*]
