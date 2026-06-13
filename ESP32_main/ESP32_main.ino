#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <L298N.h>

// =================================================================================
// 1. KHAI BÁO BIẾN TOÀN CỤC, CHÂN PHẦN CỨNG & TRẠNG THÁI
// =================================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// --- CẤU HÌNH BIẾN TRỞ B10K (CHÂN GA TỔNG) ---
const int POT_PIN = 34;          // Chân đọc biến trở B10K
int lastStableValue = 0;        // Biến lưu nấc số ổn định cũ để khóa số
const int NOISE_THRESHOLD = 15; // Ngưỡng chặn nhiễu nhấp nhô số

// --- CẤU HÌNH SERVO PHÂN LOẠI ---
Servo servoSort;
const int SERVO_PIN = 18; 
const int ANGLE_HOME = 90;  
const int ANGLE_RED  = 45;  
const int ANGLE_BLUE = 135; 

// --- CẤU HÌNH DRIVER MOTOR BĂNG CHUYỀN (L298N) ---
const int MOTOR_ENB_PIN = 14;
const int MOTOR_IN3_PIN = 27;
const int MOTOR_IN4_PIN = 26;

L298N motor(MOTOR_ENB_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN);

// --- Cấu hình thời gian trễ dịch chuyển của băng chuyền ---
const int DELAY_RED_TRAVEL  = 1500; 
const int DELAY_BLUE_TRAVEL = 3000; 
const int DELAY_SWEEP_HOLD  = 1000; 

// --- Biến phục vụ quản lý thời gian Servo bằng MILLIS (Non-blocking) ---
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

// Biến trigger từ cảm biến IR (Nhận lệnh từ ESP32-CAM qua UART)
bool isObjectDetected = false; 

// =================================================================================
// 2. HÀM SETUP (KHỞI TẠO HỆ THỐNG)
// =================================================================================
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  
  // --- Khởi tạo màn hình LCD ---
  Wire.begin(21, 22); 
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("ESP32 started");
  lcd.setCursor(0, 1); lcd.print("Wait ESP32-CAM..");

  // --- Driver L298N - Motor ---
  pinMode(MOTOR_ENB_PIN, OUTPUT);
  pinMode(MOTOR_IN3_PIN, OUTPUT);
  pinMode(MOTOR_IN4_PIN, OUTPUT);

  // --- Khởi tạo Timer cho Servo ---
  ESP32PWM::allocateTimer(0);
  servoSort.setPeriodHertz(50);
  servoSort.attach(SERVO_PIN, 500, 2400);
  servoSort.write(ANGLE_HOME);
  
  Serial.println("\n>>> ESP32 CONTROLLER SẴN SÀNG HOẠT ĐỘNG VỚI B10K CHÂN GA!");
}

// =================================================================================
// 3. VÒNG LẶP CHÍNH (XỬ LÝ DỮ LIỆU UART & ĐIỀU KHIỂN CƠ KHÍ)
// =================================================================================
void loop() {
  // -------------------------------------------------------------------------------
  // BƯỚC A: ĐỌC VÀ KHÓA CỨNG SỐ BIẾN TRỞ B10K CHỐNG NHẢY SỐ (Xử lý Chân Ga)
  // -------------------------------------------------------------------------------
  int currentValue = analogRead(POT_PIN);
  
  // Thuật toán Hysteresis chặn nhấp nhô số khi để yên tay
  if (abs(currentValue - lastStableValue) > NOISE_THRESHOLD) {
    lastStableValue = currentValue; 
  }
  
  int processedValue = lastStableValue;
  
  // Ép vùng chết cơ khí (Min: 50, Max: 4050 dựa theo log thực tế của bạn)
  if (processedValue < 50)   processedValue = 0;
  if (processedValue > 4050) processedValue = 4050;
  
  // Tính tốc độ gốc (Mức PWM Max an toàn cho Motor của bạn là 188)
  float phanTramPOT = (float)processedValue / 4050.0;
  int baseSpeed = phanTramPOT * 188; 

  // -------------------------------------------------------------------------------
  // BƯỚC B: KIỂM TRA PHANH ECO TỪ CẢM BIẾN HỒNG NGOẠI (Giảm 1/2 công suất)
  // -------------------------------------------------------------------------------
  int finalSpeed = baseSpeed; // Mặc định tốc độ thực tế bằng tốc độ tay vặn
  
  if (isObjectDetected) {
    finalSpeed = baseSpeed / 2; // Nếu IR phát hiện vật -> Ép giảm đi 1/2 công suất chân ga
  }

  // Cấp tốc độ cuối cùng cho L298N và duy trì băng chuyền quay liên tục
  motor.setSpeed(finalSpeed);
  motor.backward(); 

  // -------------------------------------------------------------------------------
  // BƯỚC C: HIỆU ỨNG DẤU CHẤM CHẠY LCD (NON-BLOCKING)
  // -------------------------------------------------------------------------------
  if (currentWifiState == STATE_TRYING) {
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

  // Hỏi thăm trạng thái wifi của CAM nếu lệch pha khởi động
  if (!hasWifiInfo && (millis() - lastRequestTime >= requestInterval)) {
    lastRequestTime = millis();
    Serial2.println("REQ:WIFI");
  }

  // -------------------------------------------------------------------------------
  // BƯỚC D: ĐỌC DỮ LIỆU UART TỪ ESP32-CAM VÀ TRIGGER CÁC SỰ KIỆN
  // -------------------------------------------------------------------------------
  if (Serial2.available() > 0) {
    String dataCam = Serial2.readStringUntil('\n');
    dataCam.trim(); 
    
    if (dataCam.length() > 0) {
      // Nhận lệnh từ Cảm biến hồng ngoại (IR) nối bên phía ESP32-CAM
      if (dataCam.startsWith("IR:")) {
        String irStatus = dataCam.substring(3);
        if (irStatus == "TRIGGERED") {
          isObjectDetected = true; // Kích hoạt cờ báo giảm nửa tốc độ băng chuyền
          lcd.setCursor(0, 1);
          lcd.print("IR: DETECTED    "); 
        }
      }

      // Nhận dữ liệu màu sắc phân loại vật thể
      else if (dataCam.startsWith("COLOR:")) {
        String colorValue = dataCam.substring(6); 
        
        if (colorValue != lastColor) {
          lastColor = colorValue; 

          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Detect:");
          lcd.setCursor(0, 1);

          if (colorValue == "RED") {
            lcd.print("RED");
            targetAngle = ANGLE_RED;
            currentTravelDelay = DELAY_RED_TRAVEL;
            servoActionStartTime = millis();
            servoState = SERVO_WAITING_TRAVEL; 
          } 
          else if (colorValue == "BLUE") {
            lcd.print("BLUE");
            targetAngle = ANGLE_BLUE;
            currentTravelDelay = DELAY_BLUE_TRAVEL;
            servoActionStartTime = millis();
            servoState = SERVO_WAITING_TRAVEL; 
          }
          else if (colorValue == "EMPTY") {
            lcd.print("EMPTY");
            isObjectDetected = false; // Băng chuyền trống -> Trả lại quyền quyết định 100% cho B10K
            servoSort.write(ANGLE_HOME);
            servoState = SERVO_IDLE;
          }
        }
      }
      
      // (Giữ nguyên các khối xử lý WIFI:TRY, WIFI:IP, WIFI:ERR để hiển thị LCD như cũ của bạn...)
      else if (dataCam.startsWith("WIFI:TRY:")) {
        String newSsid = dataCam.substring(9); 
        if (currentSsid != "" && newSsid != currentSsid) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Connect Failed!");
          lcd.setCursor(0, 1); lcd.print(currentSsid);
          delay(2000); 
        }
        currentSsid = newSsid;
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Connecting");
        lcd.setCursor(0, 1); lcd.print(currentSsid);
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
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(ssidName);  
        lcd.setCursor(0, 1); lcd.print(ipAddress); 
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
        if (!errorBlinked) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("All Failed!");
          lcd.setCursor(0, 1); lcd.print("Switching to AP");
          delay(2500); 
          errorBlinked = true;
        }
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(apSsid);
        lcd.setCursor(0, 1); lcd.print(apIp);
        currentWifiState = STATE_AP_MODE;
        hasWifiInfo = true; 
      }
    }
  }

  // -------------------------------------------------------------------------------
  // BƯỚC E: BỘ QUẢN LÝ TIẾN TRÌNH TIẾN LÙI SERVO KHÔNG DELAY (MILLIS STATE MACHINE)
  // -------------------------------------------------------------------------------
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