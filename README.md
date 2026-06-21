# Hệ Thống Phân Loại Màu Sắc (HeThongPhanLoaiSources)

![Platform](https://img.shields.io/badge/Platform-ESP32-blue?logo=espressif)
![Language](https://img.shields.io/badge/Language-C%2B%2B-orange?logo=c%2B%2B)
![Framework](https://img.shields.io/badge/Framework-Arduino_Core-green?logo=arduino)
![School](https://img.shields.io/badge/School-PTIT_HCMC-red)

Hệ thống tự động phân loại đối tượng theo màu sắc dựa trên hai ESP32, kết hợp xử lý hình ảnh và điều khiển động lực thời gian thực.

## Tổng Quan Dự Án

- **Mục tiêu**: Thiết kế và chế tạo hệ thống băng chuyền tự động phân loại vật thể theo màu sắc (Đỏ / Xanh Dương / Trống) ứng dụng thị giác máy tính.
- **Nền tảng**: Hệ thống xử lý phân tán sử dụng 02 mạch ESP32, trong đó ESP32-CAM đảm nhiệm xử lý ảnh và ESP32 NodeMCU đảm nhiệm điều khiển động lực.
- **Công nghệ cốt lõi**: Arduino Core, nhận diện không gian màu HSV, giao diện giám sát Web UI, giao tiếp UART thời gian thực.
- **Ngôn ngữ lập trình**: C++ (100%).
- **Bối cảnh**: Đồ án thực hành môn học chuyên ngành Tự động hóa, Học viện Công nghệ Bưu chính Viễn thông (PTIT), cơ sở TP. Hồ Chí Minh.

## Kiến Trúc Hệ Thống

Hệ thống gồm 2 khối điều khiển phối hợp:

### 1. ESP32-CAM, khối xử lý ảnh

- Chụp ảnh và phân tích ma trận điểm ảnh theo thời gian thực.
- Phân loại trạng thái vật thể: Đỏ, Xanh Dương, Trống dựa trên không gian màu HSV.
- Cung cấp giao diện Web UI và hệ thống REST API để giám sát trực quan.
- Quản lý Wi-Fi thông minh, tự động phát Access Point `ESP32-CAM-SETUP` để cấu hình mạng khi mất kết nối.
- Truyền kết quả phân tích xuống mạch chính qua UART (`Serial1`).

### 2. ESP32 Main, khối điều khiển động lực

- Nhận dữ liệu màu sắc và trạng thái đồng bộ Wi-Fi từ ESP32-CAM qua UART (`Serial2`).
- Thu thập giá trị biến trở xoay B10K, áp dụng bộ lọc nhiễu hysteresis để điều khiển tốc độ động cơ băng tải qua L298N.
- Điều khiển servo MG90S thực hiện cơ cấu gạt phân loại vật thể về đúng khay chứa.
- Hiển thị trạng thái vận hành, tên mạng Wi-Fi, địa chỉ IP và kết quả nhận diện lên màn hình LCD 1602 I2C.

## Cấu Trúc Thư Mục

```text
HeThongPhanLoaiSources/
├── README.md              # Tài liệu dự án
├── ESP32_CAM/
│   ├── ESP32_CAM.ino      # Camera, nhận diện màu HSV, quản lý Wi-Fi, REST API Server
│   └── web_pages.h        # Giao diện dashboard Web
└── ESP32_main/
    └── ESP32_main.ino     # Điều khiển motor L298N, servo, LCD và nhận lệnh UART
```

## Thư Viện Sử Dụng

- `Arduino.h`: Thư viện cốt lõi nhúng.
- `esp_camera.h` và `img_converters.h`: Xử lý và chuyển đổi định dạng ảnh camera.
- `esp_http_server.h`: Dựng máy chủ HTTP Server trên ESP32.
- [ESP32_OV5640_AF.h](https://github.com/0015/ESP32-OV5640-AF): Thư viện tối ưu lấy nét tự động cho cảm biến OV5640.
- `WiFi.h` và `Preferences.h`: Quản lý kết nối mạng và lưu cấu hình Wi-Fi vào bộ nhớ Flash.
- `Wire.h` và [LiquidCrystal_I2C.h](https://github.com/johnrickman/LiquidCrystal_I2C): Giao tiếp màn hình LCD qua bus I2C.
- [ESP32Servo.h](https://madhephaestus.github.io/ESP32Servo/annotated.html): Điều khiển servo chuẩn xác trên nền ESP32.
- [L298N.h](https://github.com/AndreaLombardo/L298N): Điều khiển hướng và tốc độ động cơ DC qua cầu H.

## FreeRTOS Trong Dự Án

Hệ thống tận dụng lớp điều phối nền FreeRTOS tích hợp sẵn trong Arduino Core của ESP32 để đảm bảo tính đa nhiệm song song và tránh nghẽn do xử lý tuần tự.

### 1. Phân tải tài nguyên giữa 2 chip

- **ESP32-CAM**
  - **CPU/RAM**: Chụp ảnh, bóc tách ma trận màu ROI, tính toán không gian màu HSV, phục vụ client qua Web Server và duy trì kết nối Wi-Fi.
  - **Bộ nhớ**: Chiếm dụng dung lượng RAM lớn cho bộ đệm ảnh dùng chung `shared_buf` (160,000 bytes) và framebuffer.
- **ESP32 Main**
  - **Nhiệm vụ**: Tập trung chạy vòng lặp điều khiển liên tục, gồm quét biến trở B10K, lắng nghe UART từ `Serial2`, điều xung PWM cho L298N, quét góc servo và cập nhật dữ liệu LCD mà không bị trễ.

### 2. Đồng bộ hóa bằng mutex (`frame_mutex`)

- **Cơ chế**: Khởi tạo bằng `xSemaphoreCreateMutex()`, kiểm soát tranh chấp tài nguyên bằng `xSemaphoreTake()` và `xSemaphoreGive()`.
- **Mục đích**: Khóa độc quyền phân vùng đệm ảnh tại một thời điểm, ngăn luồng ghi đệm ảnh của máy chủ web xung đột với luồng ghi khung hình mới từ camera, tránh sọc ảnh hoặc treo bộ đệm PSRAM.

### 3. Tầm quan trọng nếu không có FreeRTOS

Hệ thống sẽ rơi vào trạng thái xử lý đơn luồng tuần tự.

- Khi ESP32-CAM bận xử lý dữ liệu ảnh hoặc truyền gói tin Wi-Fi, luồng nhận UART có thể bị mất gói dữ liệu.
- Khi ESP32 Main phải chờ màn hình LCD cập nhật xong, động cơ băng chuyền và servo có thể bị khựng hoặc trễ rõ rệt.

## Chức Năng Chính

### A. Xử Lý Hình Ảnh và Phân Loại

- Chụp ảnh và trích xuất ma trận màu.
- Tính giá trị RGB trung bình từ vùng ROI kích thước 140x140 pixel đặt tại tâm bức ảnh, tương đương 19.600 điểm ảnh xử lý.
- Chuyển đổi RGB trung bình sang không gian màu HSV với:
  - $H \in [0, 360]$
  - $S \in [0, 100]$
  - $V \in [0, 100]$
- Tính sai biệt tương phản điểm ảnh liền kề để đo độ sắc nét (`sharpness`) phục vụ auto focus.

**Giải thuật phân loại:**

- **TRỐNG (EMPTY)**: Khi độ sai lệch màu so với nền băng tải trượt nhỏ hơn ngưỡng (`bg_diff < 40`) hoặc độ sáng quá thấp ($V < 15.0$).
- **VẬT ĐỎ (RED)**: Khi độ bão hòa màu $S > 25.0$ và góc màu H nằm trong dải đỏ ($H < 45.0^\circ$ hoặc $H > 315.0^\circ$).
- **VẬT XANH DƯƠNG (BLUE)**: Khi độ bão hòa màu $S > 12.0$ và góc màu H nằm trong dải xanh dương ($150.0^\circ \le H \le 275.0^\circ$).

### B. Kích Hoạt và Hành Vi Thời Gian Thực

- Cảm biến hồng ngoại (IR) phát hiện vật cản qua ngắt sườn xuống (active-low), tích hợp bộ lọc chống rung cơ học debounce 1 giây để kích hoạt tác vụ chụp ảnh `takeSnapshot()`.
- Hỗ trợ kích hoạt chụp ảnh và phân tích màu bất kỳ lúc nào qua endpoint `/capture` trên trình duyệt Web.

### C. Web API và Dashboard Giám Sát

**Các endpoint chính:**

- `/jpg`: Truy xuất khung hình JPEG snapshot mới nhất từ bộ đệm.
- `/color`: Trả về chuỗi dữ liệu JSON thời gian thực gồm giá trị RGB, HSV, nhãn màu nhận diện, độ nét, vị trí ROI và số khung hình.
- `/wifi_list` và `/delete_wifi`: Quản lý danh sách các mạng Wi-Fi đã lưu.
- `/calibrate`: Đồng bộ màu sắc hiện tại của băng tải làm màu nền chuẩn trực tiếp qua giao diện Web.

### D. Quản Lý Kết Nối Wi-Fi Thông Minh

- Lưu trữ tối đa 10 cấu hình mạng Wi-Fi (SSID/Password) trong bộ nhớ Flash thông qua thư viện `Preferences`.
- Dò và thử kết nối tuần tự theo danh sách ưu tiên gần nhất.
- Nếu tất cả kết nối thất bại, tự động chuyển sang chế độ phát Access Point (AP) với tên `ESP32-CAM-SETUP`, IP mặc định `192.168.4.1`.
- Đồng bộ trạng thái Wi-Fi qua UART xuống LCD:
  - `WIFI:TRY:<ssid>`: Đang cố gắng kết nối.
  - `WIFI:IP:<ssid>:<ip>`: Đã kết nối thành công và có địa chỉ IP hoạt động.
  - `WIFI:ERR:<ap>:<ip>`: Lỗi kết nối, đang chạy chế độ phát cấu hình AP.

### E. Hành Vi Điều Khiển và Chấp Hành

- **Động cơ băng chuyền**: Chạy liên tục theo chiều tiến, tự động giảm tốc độ xuống 1/2 tốc độ định mức khi cảm biến IR phát hiện có vật cản để giảm quán tính và tránh lệch vị trí vật.
- **Cơ chế gạt phân loại**:
  - Nhận `COLOR:RED` -> Dừng băng chuyền ngay lập tức trong 3 giây, servo quay sang góc 5° và giữ vị trí gạt trong 5 giây trước khi hồi vị.
  - Nhận `COLOR:BLUE` -> Dừng băng chuyền ngay lập tức trong 3 giây, servo quay sang góc 120° và giữ vị trí gạt trong 5 giây trước khi hồi vị.
  - Nhận `COLOR:EMPTY` -> Giữ servo tại góc nghỉ mặc định 60° (`ANGLE_HOME`) để vật trôi tự do qua băng chuyền.
- **Màn hình LCD**: Đồng bộ hiển thị trạng thái kết nối Wi-Fi, địa chỉ IP mạng và nhãn màu sắc vật thể nhận diện theo thời gian thực.

## Định Dạng Giao Thức UART

**Từ ESP32-CAM -> ESP32 Main, baudrate 115200:**

- `COLOR:RED`
- `COLOR:BLUE`
- `COLOR:EMPTY`
- `WIFI:TRY:<ssid>`
- `WIFI:IP:<ssid>:<ip>`
- `WIFI:ERR:<ap>:<ip>`
- `IR:TRIGGERED`
- `PING`: Xung đồng bộ giữ kết nối định kỳ 4 giây.

**Từ ESP32 Main -> ESP32-CAM:**

- `REQ:WIFI`: Yêu cầu gửi lại trạng thái kết nối Wi-Fi để cập nhật LCD.

## Danh Mục Thiết Bị

| Nhóm | Thành phần |
| --- | --- |
| Khối xử lý và điều khiển | 01 x Mạch ESP32-CAM tích hợp đế nạp USB CH340; 01 x Mạch ESP32 NodeMCU (30 chân) |
| Khối cảm biến | 01 x Cảm biến hình ảnh OV5640 AF (Auto Focus); 01 x Cảm biến tiệm cận hồng ngoại (IR Sensor) |
| Khối chấp hành | 01 x Mạch cầu H L298N; 01 x Động cơ DC giảm tốc (12V); 01 x Động cơ Servo MG90S (bánh răng kim loại) |
| Khối hiển thị | 01 x Màn hình LCD 1602 kèm mạch chuyển đổi giao tiếp I2C |
| Khối nguồn và cơ khí | Nguồn Adapter ổn áp 12V-1A; khung cơ khí băng tải gỗ/nhôm; biến trở đơn xoay B10K |

### Sơ Đồ Cấp Nguồn Hệ Thống

Để đảm bảo triệt tiêu sụt áp gây nhiễu và treo chip, hệ thống sử dụng cơ chế cấp nguồn độc lập:

- **Nhánh 1**: Đường nguồn 5V cấp trực tiếp cho ESP32 NodeMCU, màn hình LCD 1602 và servo MG90S.
- **Nhánh 2**: Nguồn chính 12V-1A cấp trực tiếp vào module L298N để kéo động cơ DC băng chuyền, đồng thời hạ áp cấp cho ESP32-CAM để đảm bảo độ ổn định công suất cho tác vụ chụp ảnh và truyền phát sóng Wi-Fi.

## Thành Viên Thực Hiện

- Nguyễn Bảo Toàn **(SirMaku)**
- Trần Quang Huy **(Dr.Wonder)**
- Nguyễn Xuân Khoa **(KaKa)**

## Ghi Chú Bản Quyền

> **Tài liệu và mã nguồn hệ thống được hoàn thiện dựa trên quá trình nghiên cứu thực tế môn học Tự động hóa. Hệ thống đã hoạt động ổn định và chính xác 100% theo các yêu cầu kỹ thuật đề ra.**