#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <L298N.h>

// khai báo biến

LiquidCrystal_I2C lcd(0x27, 16, 2); 

// --- CẤU HÌNH BIẾN TRỞ B10K (CHÂN GA TỔNG) ---
const int POT_PIN = 34;          // Chân đọc biến trở B10K
int lastStableValue = 0;        // Biến lưu nấc số ổn định cũ để khóa số
const int CHAN_NHIEU = 15;    // Ngưỡng chặn nhiễu nhấp nhô số

// --- điều chỉnh servo ---
Servo servoSort;
const int SERVO_PIN = 18; 
const int ANGLE_HOME = 90;  
const int ANGLE_RED  = 45;  
const int ANGLE_BLUE = 135; 

// --- cấu hình motor drive L298N ---
const int MOTOR_ENB_PIN = 14;
const int MOTOR_IN3_PIN = 27;
const int MOTOR_IN4_PIN = 26;

L298N motor(MOTOR_ENB_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN);

// --- Cấu hình thời gian trễ dịch chuyển của băng chuyền ---
const int DELAY_RED_TRAVEL  = 1500; 
const int DELAY_BLUE_TRAVEL = 3000; 
const int DELAY_SWEEP_HOLD  = 1000; 

// --- Biến thời gian Servo bằng MILLIS ---
enum ServoTaskState { SERVO_IDLE, SERVO_WAITING_TRAVEL, SERVO_HOLDING_SWEEP };
ServoTaskState servoState = SERVO_IDLE;

unsigned long servoActionStartTime = 0;
int targetAngle = ANGLE_HOME;
unsigned long currentTravelDelay = 0;

// --- Khai báo các trạng thái WiFi phục vụ hiệu ứng LCD ---
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

// trigger từ cảm biến IR
bool isObjectDetected = false; 

// --- Biến chẩn đoán lỗi kết nối phần cứng ---
unsigned long lastCamMessageTime = 0; // Thời điểm cuối cùng nhận tin từ CAM
bool isCamOnline = true;              // Trạng thái hoạt động của CAM
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
  Serial.println("\n>>> ESP32 CONTROLLER da mo");
}

void loop() {
  int currentValue = analogRead(POT_PIN);
  
  // Kiểm tra chẩn đoán biến trở B10K
  static unsigned long lastPotCheckTime = 0;
  if (millis() - lastPotCheckTime > 5000) {
    lastPotCheckTime = millis();
    if (currentValue == 0 || currentValue >= 4095) {
      Serial.printf(">>> [CANH BAO BIEN TRO] Gia tri B10K dang cham nguong cuc han (%d).\n", currentValue);
    }
  }

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
  if (isObjectDetected) {
    finalSpeed = baseSpeed / 2; // giảm theo phép chia
  }

  motor.setSpeed(finalSpeed);
  motor.backward(); 

  // Chỉ là hiệu ứng thông báo :D
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

  // check trạng thái wifi của CAM nếu lệch pha khởi động
  if (!hasWifiInfo && (millis() - lastRequestTime >= requestInterval)) {
    lastRequestTime = millis();
    Serial2.println("REQ:WIFI");
  }

  // ĐỌC DỮ LIỆU UART TỪ ESP32-CAM VÀ TRIGGER

  if (Serial2.available() > 0) {
    String dataCam = Serial2.readStringUntil('\n');
    dataCam.trim(); 
    
    if (dataCam.length() > 0) {
      lastCamMessageTime = millis(); // Cập nhật mốc thời gian nhận tin nhắn gần nhất từ CAM
      
      // Nhận lệnh từ Cảm biến hồng ngoại nối bên phía ESP32-CAM
      if (dataCam.startsWith("IR:")) {
        String irStatus = dataCam.substring(3);
        if (irStatus == "TRIGGERED") {
          isObjectDetected = true; // cờ detect giảm nửa tốc độ băng chuyền
          if (isLcdConnected) {
            lcd.setCursor(0, 1);
            lcd.print("IR: DETECTED    "); 
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
            currentTravelDelay = DELAY_RED_TRAVEL;
            servoActionStartTime = millis();
            servoState = SERVO_WAITING_TRAVEL; 
          } 
          else if (colorValue == "BLUE") {
            if (isLcdConnected) lcd.print("BLUE");
            targetAngle = ANGLE_BLUE;
            currentTravelDelay = DELAY_BLUE_TRAVEL;
            servoActionStartTime = millis();
            servoState = SERVO_WAITING_TRAVEL; 
          }
          else if (colorValue == "EMPTY") {
            if (isLcdConnected) lcd.print("EMPTY");
            isObjectDetected = false; // Băng chuyền trống -> Trả lại quyền quyết định 100% cho B10K
            servoSort.write(ANGLE_HOME);
            servoState = SERVO_IDLE;
          }
        }
      }
      
      // (Giữ nguyên các khối xử lý WIFI:TRY, WIFI:IP, WIFI:ERR để hiển thị LCD như cũ của bạn...)
      else if (dataCam.startsWith("WIFI:TRY:")) {
        String newSsid = dataCam.substring(9); 
        if (currentSsid != "" && newSsid != currentSsid && isLcdConnected) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Connect Failed!");
          lcd.setCursor(0, 1); lcd.print(currentSsid);
          delay(2000); 
        }
        currentSsid = newSsid;
        if (isLcdConnected) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Connecting");
          lcd.setCursor(0, 1); lcd.print(currentSsid);
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

  // --- KIỂM TRA MẤT KẾT NỐI UART VỚI ESP32-CAM ---
  if (millis() - lastCamMessageTime > 15000) { // 15 giây không có dữ liệu từ ESP32-CAM
    if (isCamOnline) {
      isCamOnline = false;
      Serial.println(">>> [LOI] Mat tin hieu ket noi UART voi ESP32-CAM!");
      if (isLcdConnected) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("CAM OFFLINE!");
        lcd.setCursor(0, 1); lcd.print("Check RX2/TX2...");
      }
    }
  } else {
    isCamOnline = true;
  }

  // Xử lý SERVO

  switch (servoState) {
    case SERVO_WAITING_TRAVEL:
      if (millis() - servoActionStartTime >= currentTravelDelay) {
        servoSort.write(targetAngle); 
        servoActionStartTime = millis();
        servoState = SERVO_HOLDING_SWEEP; 
      }
      break;

    case SERVO_HOLDING_SWEEP:
      if (millis() - servoActionStartTime >= DELAY_SWEEP_HOLD) {
        servoSort.write(ANGLE_HOME); 
        isObjectDetected = false;    // Hoàn thành gạt vật -> Trả lại tốc độ gốc theo biến trở lập tức
        servoState = SERVO_IDLE;      
      }
      break;

    case SERVO_IDLE:
    default:
      break;
  }
}