#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <L298N.h>

// khai báo biến
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// --- B10K ---
const int POT_PIN = 34;          // Chân B10K
int lastStableValue = 0;        // Biến lưu nấc số ổn định cũ để khóa số
const int CHAN_NHIEU = 15;    // Ngưỡng chặn nhiễu số

// --- điều chỉnh servo ---
Servo servoSort;
const int SERVO_PIN = 18; 
const int ANGLE_HOME = 60;   // Góc nghỉ trung gian mới
const int ANGLE_RED  = 10;   // Góc gạt riêng biệt cho vật đỏ
const int ANGLE_BLUE = 130; 

// --- cấu hình motor drive L298N ---
const int MOTOR_ENB_PIN = 14;
const int MOTOR_IN3_PIN = 27;
const int MOTOR_IN4_PIN = 26;

L298N motor(MOTOR_ENB_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN);

// --- delay dịch chuyển của băng chuyền ---
const int DELAY_RED_TRAVEL  = 1500; 
const int DELAY_BLUE_TRAVEL = 3000; 
const int DELAY_SWEEP_HOLD  = 5000; // Tăng thời gian giữ gạt lên 5 giây theo yêu cầu

// --- Biến thời gian Servo bằng MILLIS ---
enum ServoTaskState { SERVO_IDLE, SERVO_WAITING_TRAVEL, SERVO_HOLDING_SWEEP };
ServoTaskState servoState = SERVO_IDLE;

unsigned long servoActionStartTime = 0;
int targetAngle = ANGLE_HOME;
unsigned long currentTravelDelay = 0;

enum WifiState { STATE_WAITING, STATE_TRYING, STATE_CONNECTED, STATE_AP_MODE };
WifiState currentWifiState = STATE_WAITING;

String currentSsid = ""; 
unsigned long lastDotTime = 0; 
int dotCount = 0;              

// --- Biến lưu trữ trạng thái đồng bộ & Cảm biến ---
String lastColor = "EMPTY"; 
unsigned long lastRequestTime = 0;
const unsigned long requestInterval = 3000; 
bool hasWifiInfo = false; 
bool errorBlinked = false; 

bool isObjectDetected = false; 

// --- Biến trạng thái quét màu dừng băng tải ---
bool isScanning = false;
unsigned long scanStartTime = 0;
const unsigned long DELAY_SCAN_TIMEOUT = 4000; // Thời gian tối đa dừng chờ quét màu (4 giây)

// --- Biến trễ dừng băng chuyền khi phát hiện vật ---
bool pendingScan = false;
unsigned long irTriggerTime = 0;
const unsigned long DELAY_STOP_CONVEYOR = 150; // Độ trễ dừng băng chuyền (150ms = 0.15 giây)

// --- Biến kiểm soát dừng băng tải 3 giây khi có màu ---
bool isConveyorStoppedByGat = false;

unsigned long lastCamMessageTime = 0; // Thời điểm cuối cùng nhận tin từ esp32 Cam
bool isCamOnline = true;              // Trạng thái hoạt động của esp32 Cam
bool isLcdConnected = false;          // Trạng thái hoạt động của LCD

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  
  // --- Khởi tạo và quét thử kết nối màn hình LCD ---
  Wire.begin(21, 22); // (SDA,SCL)
  
  Wire.beginTransmission(0x27);
  byte lcdError = Wire.endTransmission();
  if (lcdError == 0) {
    isLcdConnected = true;
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0); lcd.print("ESP32 started");
    lcd.setCursor(0, 1); lcd.print("Wait ESP32-CAM..");
    Serial.println(">>> [OK] LCD mo");
  } else {
    isLcdConnected = false;
    Serial.println(">>> [LOI] LCD khong the ket noi!");
  }

  // --- Driver L298N - Motor ---
  pinMode(MOTOR_ENB_PIN, OUTPUT);
  pinMode(MOTOR_IN3_PIN, OUTPUT);
  pinMode(MOTOR_IN4_PIN, OUTPUT);

  // --- Khởi tạo Timer cho Servo ---
  ESP32PWM::allocateTimer(0);
  servoSort.setPeriodHertz(50);
  servoSort.attach(SERVO_PIN, 500, 2400);
  servoSort.write(ANGLE_HOME);
  
  lastCamMessageTime = millis();
  Serial.println("\n>>> ESP32 MAIN da mo");
}

void loop() {
  int currentValue = analogRead(POT_PIN);

  // Thuật toán Hysteresis
  if (abs(currentValue - lastStableValue) > CHAN_NHIEU) {
    lastStableValue = currentValue; 
  }
  
  int processedValue = lastStableValue;
  
  if (processedValue < 50)   processedValue = 0;
  if (processedValue > 4050) processedValue = 4050;
  
  float phanTramPOT = (float)processedValue / 4050.0;
  int baseSpeed = phanTramPOT * 255; 

  int finalSpeed = baseSpeed;
  // Giảm tốc độ chạy chậm bằng 1/2 khi có vật cản để tránh vật bay ra ngoài
  if (isObjectDetected) {
    finalSpeed = baseSpeed / 2; 
  }
  // Dừng hẳn băng chuyền khi đang trong trạng thái dừng chờ quét màu tĩnh (isScanning) hoặc dừng gạt giữ (isConveyorStoppedByGat)
  if (isScanning || isConveyorStoppedByGat) {
    finalSpeed = 0; 
  }

  motor.setSpeed(finalSpeed);
  motor.forward(); 

  // Chỉ là hiệu ứng thông báo
  if (currentWifiState == STATE_TRYING && isLcdConnected) {
    if (millis() - lastDotTime >= 500) { 
      lastDotTime = millis();
      dotCount = (dotCount + 1) % 4; 
      
      lcd.setCursor(0, 0);
      String displayStr = "Connecting";
      for (int i = 0; i < dotCount; i++) {
        displayStr += ".";
      }
      while (displayStr.length() < 16) {
        displayStr += " ";
      }
      lcd.print(displayStr);
    }
  }

  // check trạng thái wifi của esp32 Cam nếu lệch pha khởi động
  if (!hasWifiInfo && (millis() - lastRequestTime >= requestInterval)) {
    lastRequestTime = millis();
    Serial2.println("REQ:WIFI");
  }

  // XỬ LÝ TRỄ DỪNG BĂNG CHUYỀN SAU KHI IR TRIGGER
  if (pendingScan && (millis() - irTriggerTime >= DELAY_STOP_CONVEYOR)) {
    pendingScan = false;
    isScanning = true;            // Dừng hẳn băng chuyền để kích hoạt quét màu
    scanStartTime = millis();     // Lưu mốc thời gian bắt đầu quét màu
    lastColor = "EMPTY";          // Sẵn sàng đón màu mới
    if (isLcdConnected) {
      lcd.setCursor(0, 1);
      lcd.print("SCANNING...     "); 
    }
  }

  // XỬ LÝ NHẢ CỜ DỪNG BĂNG CHUYỀN 3 GIÂY KHI ĐANG GẠT GIỮ
  if (isConveyorStoppedByGat && (millis() - servoActionStartTime >= 3000)) {
    isConveyorStoppedByGat = false; // Sau 3 giây, cho băng chuyền chạy lại để đẩy vật đi tiếp
  }

  // Đọc dữ liệu từ ESP32-CAM
  if (Serial2.available() > 0) {
    String dataCam = Serial2.readStringUntil('\n');
    dataCam.trim(); 
    
    if (dataCam.length() > 0) {
      lastCamMessageTime = millis();
      
      if (dataCam == "PING") {
      }
      else if (dataCam.startsWith("CAM_ERR:")) {
        String errType = dataCam.substring(8);
        if (errType == "INIT_FAIL") {
          Serial.println(">>> [LOI] ESP32: Khoi dong CAM that bai!");
          if (isLcdConnected) {
            lcd.clear();
            lcd.setCursor(0, 0); lcd.print("CAM INIT FAIL!");
          }
          hasWifiInfo = true;
        }
      }

      else if (dataCam.startsWith("IR:")) {
        String irStatus = dataCam.substring(3);
        if (irStatus == "TRIGGERED") {
          // Kích hoạt tiến trình trễ dừng băng tải thay vì dừng lập tức
          if (!isScanning && !pendingScan) {
            pendingScan = true;
            irTriggerTime = millis();
            isObjectDetected = true; // Bật cờ giảm tốc độ băng chuyền mượt mà
          }
        }
      }

      // Nhận dữ liệu màu sắc phân loại vật thể
      else if (dataCam.startsWith("COLOR:")) {
        String colorValue = dataCam.substring(6); 
        
        if (colorValue != lastColor) {
          lastColor = colorValue; 

          if (isLcdConnected) {
            lcd.clear();
            lcd.setCursor(0, 0); lcd.print("Detect:");
            lcd.setCursor(0, 1);
          }

          if (colorValue == "RED") {
            if (isLcdConnected) lcd.print("RED");
            targetAngle = ANGLE_RED;
            isScanning = false;
            pendingScan = false;
            isConveyorStoppedByGat = true; // Dừng băng chuyền lập tức trong 3 giây
            servoSort.write(targetAngle);  // Gạt ngay lập tức về vị trí Đỏ
            servoActionStartTime = millis();
            servoState = SERVO_HOLDING_SWEEP; 
          } 
          else if (colorValue == "BLUE") {
            if (isLcdConnected) lcd.print("BLUE");
            targetAngle = ANGLE_BLUE;
            isScanning = false;
            pendingScan = false;
            isConveyorStoppedByGat = true; // Dừng băng chuyền lập tức trong 3 giây
            servoSort.write(targetAngle);  // Gạt ngay lập tức về vị trí Xanh
            servoActionStartTime = millis();
            servoState = SERVO_HOLDING_SWEEP; 
          }
          else if (colorValue == "EMPTY") {
            // Chỉ cho phép xử lý trạng thái rỗng khi không quét, không chờ quét và không gạt giữ
            if (!isScanning && !pendingScan && servoState == SERVO_IDLE) {
              if (isLcdConnected) lcd.print("EMPTY");
              isObjectDetected = false; 
              servoSort.write(ANGLE_HOME);
            }
          }
        }
      }
      
      else if (dataCam.startsWith("WIFI:TRY:")) {
        String newSsid = dataCam.substring(9); 
        
        Serial.println(">>> [WIFI] Dang thu ket noi mang: " + newSsid);
        
        bool isPrevRealSsid = (currentSsid != "" && currentSsid != "Reconnecting..." && currentSsid != "Connecting...");
        bool isNewRealSsid = (newSsid != "" && newSsid != "Reconnecting..." && newSsid != "Connecting...");
        
        if (isPrevRealSsid && isNewRealSsid && newSsid != currentSsid && isLcdConnected) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Connect Failed!");
          lcd.setCursor(0, 1); lcd.print(currentSsid);
          delay(2000); 
        }
        
        currentSsid = newSsid;
        if (isLcdConnected) {
          lcd.clear();
          lcd.setCursor(0, 0); 
          if (newSsid == "Reconnecting...") {
            lcd.print("Reconnecting");
          } else {
            lcd.print("Connecting");
          }
          lcd.setCursor(0, 1); 
          lcd.print(currentSsid);
        }
        currentWifiState = STATE_TRYING;
        lastDotTime = millis();
        dotCount = 0;
        hasWifiInfo = false;
        errorBlinked = false; 
      }
      else if (dataCam.startsWith("WIFI:IP:")) {
        String ipContent = dataCam.substring(8); 
        int colonIdx = ipContent.indexOf(':');
        String ssidName = "WiFi Connected"; 
        String ipAddress = ipContent;
        if (colonIdx != -1) {
          ssidName = ipContent.substring(0, colonIdx);    
          ipAddress = ipContent.substring(colonIdx + 1); 
        }
        
        Serial.println(">>> [WIFI] Da ket noi thanh cong! SSID: " + ssidName + " - IP: " + ipAddress);
        
        if (isLcdConnected) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print(ssidName);  
          lcd.setCursor(0, 1); lcd.print(ipAddress); 
        }
        currentWifiState = STATE_CONNECTED;
        hasWifiInfo = true;
        errorBlinked = false;
      }
      else if (dataCam.startsWith("WIFI:ERR:")) {
        String errContent = dataCam.substring(9); 
        int colonIdx = errContent.indexOf(':');
        String apSsid = "ESP32-CAM-SETUP";
        String apIp = "192.168.4.1";
        if (colonIdx != -1) {
          apSsid = errContent.substring(0, colonIdx);
          apIp = errContent.substring(colonIdx + 1);
        }
        
        Serial.println(">>> [WIFI] LOI KET NOI! Tu dong phat AP: " + apSsid + " - IP: " + apIp);
        
        if (!errorBlinked && isLcdConnected) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("All Failed!");
          lcd.setCursor(0, 1); lcd.print("Switching to AP");
          delay(2500); 
          errorBlinked = true;
        }
        if (isLcdConnected) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print(apSsid);
          lcd.setCursor(0, 1); lcd.print(apIp);
        }
        currentWifiState = STATE_AP_MODE;
        hasWifiInfo = true; 
      }
    }
  }

  // --- KIỂM TRA KẾT NỐI VỚI ESP32-CAM ---
  if (millis() - lastCamMessageTime > 15000) { // 15 giây không có dữ liệu từ esp32 Cam
    if (isCamOnline) {
      isCamOnline = false;
      Serial.println(">>> [LOI] Mat tin hieu ket noi UART voi esp32 Cam!");
      if (isLcdConnected) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("CAM OFFLINE!");
      }
    }
  } else {
    isCamOnline = true;
  }

  // --- KIỂM TRA QUÁ THỜI GIAN CHỜ QUÉT MÀU (SCAN TIMEOUT) ---
  if (isScanning && (millis() - scanStartTime >= DELAY_SCAN_TIMEOUT)) {
    isScanning = false;
    pendingScan = false;
    isObjectDetected = false;
    isConveyorStoppedByGat = false;
    if (isLcdConnected) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Scan Timeout!");
    }
    servoSort.write(ANGLE_HOME);
    servoState = SERVO_IDLE;
  }

  // Xử lý SERVO
  switch (servoState) {
    case SERVO_HOLDING_SWEEP:
      if (millis() - servoActionStartTime >= DELAY_SWEEP_HOLD) {
        servoSort.write(ANGLE_HOME); // Trở về vị trí nghỉ trung gian (60 độ) sau khi giữ gạt xong (5 giây)
        isObjectDetected = false;    // Hoàn thành gạt vật -> Cho phép chạy băng chuyền lại bình thường
        isConveyorStoppedByGat = false;
        servoState = SERVO_IDLE;      
      }
      break;

    case SERVO_WAITING_TRAVEL:
    case SERVO_IDLE:
    default:
      break;
  }
}