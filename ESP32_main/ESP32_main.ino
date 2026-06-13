#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <L298N.h>

// =================================================================================
// 1. KHAI BÁO BIẾN TOÀN CỤC, CHÂN PHẦN CỨNG & TRẠNG THÁI
// =================================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// Khai báo đối tượng Servo duy nhất làm nhiệm vụ phân loại
Servo servoSort;
const int SERVO_PIN = 18; // Điều khiển Servo gạt bằng chân 18

// --- CẤU HÌNH GÓC QUAY CHO 1 SERVO PHÂN LOẠI 2 HƯỚNG ---
// Bạn hãy điều chỉnh các góc này cho khớp với cơ cấu gạt thực tế của mình nhé
const int ANGLE_HOME = 90;  // Vị trí ở giữa (chờ vật đi qua hoặc chuẩn bị gạt)
const int ANGLE_RED  = 45;  // Xoay sang trái để gạt vật màu ĐỎ
const int ANGLE_BLUE = 135; // Xoay sang phải để gạt vật màu XANH DƯƠNG

const int MOTOR_ENB_PIN = 14;
const int MOTOR_IN3_PIN = 27;
const int MOTOR_IN4_PIN = 26;

L298N motor(MOTOR_ENB_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN);

// --- Cấu hình thời gian trễ dịch chuyển của băng chuyền ---
const int DELAY_RED_TRAVEL    = 1500; // Thời gian vật chạy từ Camera -> Vị trí gạt Đỏ (ms)
const int DELAY_BLUE_TRAVEL   = 3000; // Thời gian vật chạy từ Camera -> Vị trí gạt Xanh (ms)
const int DELAY_SWEEP_HOLD    = 1000; // Thời gian tay gạt giữ nguyên vị trí chặn để gạt vật (ms)

// --- Khai báo các trạng thái WiFi phục vụ hiệu ứng LCD ---
enum WifiState { STATE_WAITING, STATE_TRYING, STATE_CONNECTED, STATE_AP_MODE };
WifiState currentWifiState = STATE_WAITING;

String currentSsid = ""; 
unsigned long lastDotTime = 0; // Đếm thời gian đổi số lượng dấu chấm
int dotCount = 0;              // Số lượng dấu chấm đang hiện (0 -> 3)

// --- Biến lưu trữ trạng thái đồng bộ ---
String lastColor = "EMPTY"; 
unsigned long lastRequestTime = 0;
const unsigned long requestInterval = 3000; 
bool hasWifiInfo = false; 
bool errorBlinked = false; 

// =================================================================================
// 2. HÀM SETUP (KHỞI TẠO HỆ THỐNG)
// =================================================================================
void setup() {
  // Cổng Serial để debug với máy tính
  Serial.begin(115200);
  
  // Cổng Serial2 UART giao tiếp với ESP32-CAM (RX2=16, TX2=17)
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

  motor.setSpeed(150);

  // --- Khởi tạo Timer cho Servo ---
  ESP32PWM::allocateTimer(0);

  servoSort.setPeriodHertz(50);
  servoSort.attach(SERVO_PIN, 500, 2400);

  // Đưa Servo duy nhất về góc giữa (90 độ) an toàn lúc khởi động
  servoSort.write(ANGLE_HOME);
  
  Serial.println("\n>>> ESP32 CONTROLLER SẴN SÀNG Hoạt Động!");
}

// =================================================================================
// 3. VÒNG LẶP CHÍNH (XỬ LÝ DỮ LIỆU UART & ĐIỀU KHIỂN CƠ KHÍ)
// =================================================================================
void loop() {
  motor.backward();

  // --- HIỆU ỨNG DẤU CHẤM CHẠY KHÔNG GÂY TREO MẠCH (NON-BLOCKING) ---
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

  // Gửi lệnh REQ hỏi thăm trạng thái WiFi của CAM nếu khởi động lệch pha
  if (!hasWifiInfo && (millis() - lastRequestTime >= requestInterval)) {
    lastRequestTime = millis();
    Serial2.println("REQ:WIFI");
  }

  if (Serial2.available() > 0) {
    String dataCam = Serial2.readStringUntil('\n');
    dataCam.trim(); 
    
    if (dataCam.length() > 0) {
      Serial.println(">>> RAW DATA TỪ CAM: " + dataCam); 

      // ==========================================================
      // PHẦN XỬ LÝ HIỂN THỊ TRẠNG THÁI WIFI TRÊN LCD
      // ==========================================================
      
      if (dataCam.startsWith("WIFI:TRY:")) {
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

      // ==========================================================
      // PHẦN ĐIỀU KHIỂN PHÂN LOẠI VẬT THỂ MÀU SẮC (DÙNG 1 SERVO)
      // ==========================================================
      else if (dataCam.startsWith("COLOR:")) {
        String colorValue = dataCam.substring(6); 
        
        if (colorValue != lastColor) {
          lastColor = colorValue; 

          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Detect:");
          lcd.setCursor(0, 1);

          // Trường hợp 1: Phát hiện vật thể MÀU ĐỎ
          if (colorValue == "RED") {
            lcd.print("RED");
            
            // Chờ vật di chuyển từ vị trí Camera đến vị trí gạt Đỏ
            delay(DELAY_RED_TRAVEL); 
            
            // Gạt Servo sang góc màu Đỏ (45 độ)
            servoSort.write(ANGLE_RED); 
            
            // Giữ cánh tay gạt chặn đường vật rơi
            delay(DELAY_SWEEP_HOLD); 
            
            // Thu cánh tay gạt về vị trí chờ (90 độ)
            servoSort.write(ANGLE_HOME); 
          } 
          
          // Trường hợp 2: Phát hiện vật thể MÀU XANH DƯƠNG
          else if (colorValue == "BLUE") {
            lcd.print("BLUE");
            
            // Chờ vật di chuyển từ vị trí Camera đến vị trí gạt Xanh
            delay(DELAY_BLUE_TRAVEL);
            
            // Gạt Servo sang góc màu Xanh (135 độ)
            servoSort.write(ANGLE_BLUE); 
            
            // Giữ cánh tay gạt chặn đường vật rơi
            delay(DELAY_SWEEP_HOLD); 
            
            // Thu cánh tay gạt về vị trí chờ (90 độ)
            servoSort.write(ANGLE_HOME); 
          }
          
          // Trường hợp 3: Băng chuyền trống
          else if (colorValue == "EMPTY") {
            lcd.print("EMPTY");
            
            // Đảm bảo đưa cánh tay gạt duy nhất về lại vị trí chờ (90 độ) an toàn
            servoSort.write(ANGLE_HOME);
          }
        }
      }
    }
  }
}