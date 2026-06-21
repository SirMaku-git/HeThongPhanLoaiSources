#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "ESP32_OV5640_AF.h"
#include <Preferences.h>
#include "web_pages.h"

OV5640 ov5640 = OV5640();
httpd_handle_t camera_httpd = NULL;
Preferences prefs;

bool isConfigMode = false;
bool camera_ok = false; 
#define IR_SENSOR_PIN 13 

uint8_t *shared_buf = NULL;
volatile size_t shared_len = 0;
SemaphoreHandle_t frame_mutex = NULL;

volatile uint8_t current_r = 0, current_g = 0, current_b = 0;
volatile float current_h = 0.0, current_s = 0.0, current_v = 0.0;

// Màu nền mặc định
volatile uint8_t bg_r = 170, bg_g = 180, bg_b = 167; 

volatile bool calibrated = false;
volatile int roi_x = 90, roi_y = 50;
volatile float current_sharpness = 0.0;
volatile uint32_t current_frame = 0; 

String current_label = "TRỐNG";
String lastSsidTried = "Connecting...";

void takeSnapshot();

// Đo và phân loại theo màu HSV
void rgbToHsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
  float r_f = r / 255.0f;
  float g_f = g / 255.0f;
  float b_f = b / 255.0f;

  float max_v = max(r_f, max(g_f, b_f));
  float min_v = min(r_f, min(g_f, b_f));
  float delta = max_v - min_v;

  v = max_v * 100.0f;

  if (max_v == 0.0f) {
    s = 0.0f;
  } else {
    s = (delta / max_v) * 100.0f;
  }

  if (delta == 0.0f) {
    h = 0.0f;
  } else {
    if (max_v == r_f) {
      h = 60.0f * fmod(((g_f - b_f) / delta), 6.0f);
    } else if (max_v == g_f) {
      h = 60.0f * (((b_f - r_f) / delta) + 2.0f);
    } else {
      h = 60.0f * (((r_f - g_f) / delta) + 4.0f);
    }

    if (h < 0.0f) {
      h += 360.0f;
    }
  }
}

void processColorLogic(camera_fb_t* fb) {
  long sumR = 0;
  long sumG = 0;
  long sumB = 0;
  long sharp = 0;
  uint16_t prev_px = 0;

  for (int y = roi_y; y < roi_y + 140; y++) {
    for (int x = roi_x; x < roi_x + 140; x++) {
      int idx = (y * 320 + x) * 2;
      uint16_t px = (fb->buf[idx] << 8) | fb->buf[idx + 1];
      
      uint8_t r = ((px >> 11) & 0x1F) << 3;
      uint8_t g = ((px >> 5) & 0x3F) << 2;
      uint8_t b = (px & 0x1F) << 3;

      sumR += r;
      sumG += g;
      sumB += b;
      
      if (x > roi_x) { 
        uint8_t p_r = ((prev_px >> 11) & 0x1F) << 3;
        uint8_t p_g = ((prev_px >> 5) & 0x3F) << 2;
        uint8_t p_b = (prev_px & 0x1F) << 3;
        sharp += abs((r + g + b) / 3 - (p_r + p_g + p_b) / 3);
      }
      prev_px = px;
    }
  }

  // Chia trung bình cho 19600 (140x140) pixel
  current_r = sumR / 19600; 
  current_g = sumG / 19600; 
  current_b = sumB / 19600;
  current_sharpness = (float)sharp / 19600;
  
  float t_h = 0, t_s = 0, t_v = 0;
  rgbToHsv(current_r, current_g, current_b, t_h, t_s, t_v);
  current_h = t_h; 
  current_s = t_s; 
  current_v = t_v;

  // Tính toán mức độ sai biệt màu sắc thực tế với nền chiếu trúc
  int bg_diff = abs(current_r - bg_r) + abs(current_g - bg_g) + abs(current_b - bg_b);
  
  if (bg_diff < 40) {
    current_label = "TRỐNG"; 
  } else if (current_v > 15.0f) {
    if (current_s > 25.0f && (current_h < 45.0f || current_h > 315.0f)) {
      current_label = "VẬT ĐỎ";
    } 
    else if (current_s > 12.0f && (current_h >= 150.0f && current_h <= 275.0f)) {
      current_label = "VẬT XANH DƯƠNG";
    } 
    else {
      current_label = "TRỐNG"; 
    }
  } else {
    current_label = "TRỐNG";
  }
}

// CÁC HÀM ĐIỀU KHIỂN NGUỒN CAMERA
void suspendCamera() {
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->set_reg) {
    s->set_reg(s, 0x3008, 0xFF, 0x40); 
  }
}

void resumeCamera() {
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->set_reg) {
    s->set_reg(s, 0x3008, 0xFF, 0x02); 
  }
}

void takeSnapshot() {
  if (!camera_ok) {
    Serial1.println("COLOR:EMPTY");
    return;
  }
  if (isConfigMode) return; 

  resumeCamera();
  delay(150);

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_saturation(s, 1);
    s->set_brightness(s, -2);
    s->set_contrast(s, 2);
    s->set_ae_level(s, -1);
  }

  for (int i = 0; i < 5; i++) {
    camera_fb_t *dummy_fb = esp_camera_fb_get();
    if (dummy_fb) {
      esp_camera_fb_return(dummy_fb);
    }
    delay(30);
  }

  camera_fb_t * fb = esp_camera_fb_get(); 
  if (!fb) {
    suspendCamera();
    return;
  }

  processColorLogic(fb); 

  if (xSemaphoreTake(frame_mutex, portMAX_DELAY) == pdTRUE) {
    uint8_t *out_jpg = NULL;
    size_t out_jpg_len = 0;
    if (fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 80, &out_jpg, &out_jpg_len)) {
      memcpy(shared_buf, out_jpg, out_jpg_len);
      shared_len = out_jpg_len;
      free(out_jpg); 
    }
    xSemaphoreGive(frame_mutex); 
  }
  esp_camera_fb_return(fb); 
  current_frame++;

  suspendCamera();

  if (current_label == "VẬT ĐỎ") Serial1.println("COLOR:RED");
  else if (current_label == "VẬT XANH DƯƠNG") Serial1.println("COLOR:BLUE");
  else Serial1.println("COLOR:EMPTY");
}

// API Web
void urldecode(char *dst, const char *src) {
  char a, b;
  while (*src) {
    if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a') a -= 'a'-'A'; if (a >= 'A') a -= ('A' - 10); else a -= '0';
      if (b >= 'a') b -= 'a'-'A'; if (b >= 'A') b -= ('A' - 10); else b -= '0';
      *dst++ = 16*a+b; src+=3;
    } else if (*src == '+') { *dst++ = ' '; src++; }
    else { *dst++ = *src++; }
  }
  *dst++ = '\0';
}

esp_err_t wifi_list_handler(httpd_req_t *req) {
  prefs.begin("wifi", true); 
  int count = prefs.getInt("c", 0);
  String json = "[";
  for (int i = 0; i < count; i++) {
    String s = prefs.getString(("s"+String(i)).c_str(), "");
    String p = prefs.getString(("p"+String(i)).c_str(), "");
    json += "{\"ssid\":\"" + s + "\",\"pass\":\"" + p + "\"}";
    if (i < count - 1) json += ",";
  }
  json += "]";
  prefs.end();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), json.length());
}

esp_err_t delete_wifi_handler(httpd_req_t *req) {
  char q[64], idx_s[10];
  int delete_idx = -1;
  
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "idx", idx_s, sizeof(idx_s)) == ESP_OK) {
      delete_idx = atoi(idx_s);
    }
  }
  
  if (delete_idx < 0) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Index khong hop le");
  }

  prefs.begin("wifi", false);
  int count = prefs.getInt("c", 0);
  
  if (delete_idx >= count) {
    prefs.end();
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Vuot qua pham vi");
  }

  for (int i = delete_idx; i < count - 1; i++) {
    String next_s = prefs.getString(("s" + String(i + 1)).c_str(), "");
    String next_p = prefs.getString(("p" + String(i + 1)).c_str(), "");
    prefs.putString(("s" + String(i)).c_str(), next_s);
    prefs.putString(("p" + String(i)).c_str(), next_p);
  }

  prefs.remove(("s" + String(count - 1)).c_str());
  prefs.remove(("p" + String(count - 1)).c_str());
  
  prefs.putInt("c", count - 1); 
  prefs.end();

  return httpd_resp_send(req, "Da xoa mang thanh cong!", 24);
}

esp_err_t config_root_handler(httpd_req_t *req) { 
  httpd_resp_set_type(req, "text/html"); 
  return httpd_resp_send(req, SETUP_HTML, strlen(SETUP_HTML)); 
}

esp_err_t config_save_handler(httpd_req_t *req) {
  char q[128], s_raw[32], p_raw[64], s[32], p[64];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "s", s_raw, sizeof(s_raw)) == ESP_OK && httpd_query_key_value(q, "p", p_raw, sizeof(p_raw)) == ESP_OK) {
        urldecode(s, s_raw); urldecode(p, p_raw);
        prefs.begin("wifi", false);
        int count = prefs.getInt("c", 0);
        int newCount = count < 10 ? count + 1 : 10; 
        
        for (int i = newCount - 1; i > 0; i--) {
            prefs.putString(("s"+String(i)).c_str(), prefs.getString(("s"+String(i-1)).c_str(), ""));
            prefs.putString(("p"+String(i)).c_str(), prefs.getString(("p"+String(i-1)).c_str(), ""));
        }
        prefs.putString("s0", String(s)); 
        prefs.putString("p0", String(p)); 
        prefs.putInt("c", newCount);
        prefs.end();
        
        httpd_resp_send(req, "Da luu thanh cong! ESP32 dang khoi dong lai...", HTTPD_RESP_USE_STRLEN);
        delay(1500); 
        ESP.restart(); 
    }
  }
  return httpd_resp_send(req, "Loi luu mang!", 13);
}

esp_err_t root_handler(httpd_req_t *req) { 
  httpd_resp_set_type(req, "text/html"); 
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML)); 
}

esp_err_t capture_handler(httpd_req_t *req) { 
  takeSnapshot(); 
  return httpd_resp_send(req, "OK", 2); 
}

esp_err_t jpg_handler(httpd_req_t *req) {
  if (xSemaphoreTake(frame_mutex, 15) == pdTRUE) {
    if (shared_len > 0) {
      httpd_resp_set_type(req, "image/jpeg");
      esp_err_t res = httpd_resp_send(req, (const char*)shared_buf, shared_len);
      xSemaphoreGive(frame_mutex);
      return res;
    }
    xSemaphoreGive(frame_mutex);
  }
  return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Rong");
}

esp_err_t color_handler(httpd_req_t *req) {
  if (current_label == "VẬT ĐỎ") Serial1.println("COLOR:RED");
  else if (current_label == "VẬT XANH DƯƠNG") Serial1.println("COLOR:BLUE");
  else Serial1.println("COLOR:EMPTY");

  char json[300];
  snprintf(json, sizeof(json), "{\"r\":%d,\"g\":%d,\"b\":%d,\"h\":%.1f,\"s\":%.1f,\"v\":%.1f,\"label\":\"%s\",\"sharp\":%.1f,\"roi_x\":%d,\"roi_y\":%d,\"frame\":%u}", 
    current_r, current_g, current_b, current_h, current_s, current_v, current_label.c_str(), current_sharpness, roi_x, roi_y, current_frame);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, strlen(json));
}

esp_err_t calibrate_handler(httpd_req_t *req) { 
  bg_r = current_r; bg_g = current_g; bg_b = current_b; 
  return httpd_resp_send(req, "OK", 2); 
}

// Setup

void setup() {
  setCpuFrequencyMhz(80); 
  Serial.begin(115200);
  Serial.println("\n>>> KHOI DONG HE THONG esp32 Cam...");

  Serial1.begin(115200, SERIAL_8N1, 14, 15);

  pinMode(IR_SENSOR_PIN, INPUT_PULLUP); 
  pinMode(33, OUTPUT); digitalWrite(33, HIGH); 

  frame_mutex = xSemaphoreCreateMutex(); 
  shared_buf = (uint8_t *)(psramFound() ? ps_malloc(160000) : malloc(160000)); 

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_2; config.ledc_timer = LEDC_TIMER_1;     
  config.pin_d0 = 5;  config.pin_d1 = 18; config.pin_d2 = 19; config.pin_d3 = 21;
  config.pin_d4 = 36; config.pin_d5 = 39; config.pin_d6 = 34; config.pin_d7 = 35;
  config.pin_xclk = 0; config.pin_pclk = 22; config.pin_vsync = 25; config.pin_href = 23;
  config.pin_sccb_sda = 26; config.pin_sccb_scl = 27; config.pin_pwdn = 32; config.pin_reset = -1;
  config.xclk_freq_hz = 10000000; 
  config.frame_size = FRAMESIZE_QVGA;       
  config.pixel_format = PIXFORMAT_RGB565;   
  config.fb_count = 1; 
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; 
  config.fb_location = CAMERA_FB_IN_PSRAM;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println(">>> [LOI] Khong the khoi tao Camera!");
    current_label = "LỖI CAMERA";
  } else {
    camera_ok = true;
    Serial.println(">>> Camera OK!...");
    delay(1000); 

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      s->set_whitebal(s, 1); s->set_awb_gain(s, 1); s->set_exposure_ctrl(s, 1);
      s->set_gain_ctrl(s, 1); s->set_saturation(s, 1); s->set_hmirror(s, 1); s->set_vflip(s, 1);
    }

    for (int i = 0; i < 15; i++) {
      camera_fb_t * fb = esp_camera_fb_get();
      if (fb) esp_camera_fb_return(fb);
      delay(50);
    }

    suspendCamera();
  }

  prefs.begin("wifi", false);
  int wifi_count = prefs.getInt("c", 0);
  bool wifi_connected = false;

  if (wifi_count > 0) {
    WiFi.mode(WIFI_STA);
    
    for (int i = 0; i < wifi_count; i++) {
      String saved_s = prefs.getString(("s"+String(i)).c_str(), "");
      String saved_p = prefs.getString(("p"+String(i)).c_str(), "");
      
      Serial.printf(">>> Thu ket noi WiFi (%d): %s\n", i+1, saved_s.c_str());
      lastSsidTried = saved_s; 
      
      Serial1.println("WIFI:TRY:" + saved_s);
      
      WiFi.disconnect(true); 
      delay(100);
      
      WiFi.begin(saved_s.c_str(), saved_p.c_str());
      WiFi.setTxPower(WIFI_POWER_13dBm);
      
      for (int j = 0; j < 10; j++) { 
        if (WiFi.status() == WL_CONNECTED) { wifi_connected = true; break; }
        delay(500);
        Serial.print(".");
      }
      Serial.println();
      if (wifi_connected) break; 
    }
  }
  prefs.end(); 

  if (!wifi_connected) {
    Serial.println(">>> KHONG TIM THAY MANG! AP Mode");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-CAM-SETUP"); 
    WiFi.setTxPower(WIFI_POWER_17dBm);
    isConfigMode = true; 
    Serial.print(">>> Ket noi WiFi 'ESP32-CAM-SETUP' -> vao web: http://");
    Serial.println(WiFi.softAPIP());

    Serial1.println("WIFI:ERR:ESP32-CAM-SETUP:" + WiFi.softAPIP().toString());
  } else {
    Serial.print(">>> KET NOI THANH CONG! IP cua ban la: http://");
    Serial.println(WiFi.localIP());

    Serial1.println("WIFI:IP:" + WiFi.SSID() + ":" + WiFi.localIP().toString());
  }

  httpd_config_t conf = HTTPD_DEFAULT_CONFIG(); 
  conf.server_port = 80; 
  
  if (httpd_start(&camera_httpd, &conf) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/wifi_list", HTTP_GET, wifi_list_handler, NULL }); 
    httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/delete_wifi", HTTP_GET, delete_wifi_handler, NULL });

    if (isConfigMode) {
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/", HTTP_GET, config_root_handler, NULL });
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/save", HTTP_GET, config_save_handler, NULL });
    } else {
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/", HTTP_GET, root_handler, NULL });
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/capture", HTTP_GET, capture_handler, NULL });
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/jpg", HTTP_GET, jpg_handler, NULL });
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/color", HTTP_GET, color_handler, NULL });
    }
    Serial.println(">>> Server da chay!");
  }
}

// LOOP

void loop() {
  if (Serial1.available() > 0) {
    String req = Serial1.readStringUntil('\n');
    req.trim();
    if (req == "REQ:WIFI") {
      if (isConfigMode) {
        Serial1.println("WIFI:ERR:ESP32-CAM-SETUP:" + WiFi.softAPIP().toString());
      } else if (WiFi.status() == WL_CONNECTED) {
        Serial1.println("WIFI:IP:" + WiFi.SSID() + ":" + WiFi.localIP().toString());
      } else {
        Serial1.println("WIFI:TRY:" + lastSsidTried);
      }
    }
  }

  if (isConfigMode) { delay(10); return; } 

  static unsigned long last_w = 0;
  static bool last_ir_state = HIGH;
  static unsigned long last_trigger_time = 0;
  static unsigned long lastPing = 0; 

  // Kiểm tra với chu kì 4s
  if (millis() - lastPing >= 4000) {
    lastPing = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial1.println("PING");
    }
  }

  bool current_ir_state = digitalRead(IR_SENSOR_PIN);
  
  if (last_ir_state == HIGH && current_ir_state == LOW) {
    if (millis() - last_trigger_time > 1000) { 
      Serial1.println("IR:TRIGGERED");
      takeSnapshot(); 
      last_trigger_time = millis();
    }
  }
  last_ir_state = current_ir_state;

  if (millis() - last_w >= 5000) { 
    last_w = millis();
    if (WiFi.status() != WL_CONNECTED) { 
      Serial.println(">>> [WIFI] Mat ket noi! Dang thu reconnect...");
      Serial1.println("WIFI:TRY:Reconnecting...");
      WiFi.disconnect(); 
      WiFi.reconnect(); 
    }
  }
  delay(10); 
}