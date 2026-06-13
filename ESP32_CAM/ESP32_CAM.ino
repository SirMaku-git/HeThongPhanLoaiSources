#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "ESP32_OV5640_AF.h"
#include <Preferences.h>

OV5640 ov5640 = OV5640();
httpd_handle_t camera_httpd = NULL;
Preferences prefs;

bool isConfigMode = false;
#define IR_SENSOR_PIN 13 

uint8_t *shared_buf = NULL;
volatile size_t shared_len = 0;
SemaphoreHandle_t frame_mutex = NULL;

volatile uint8_t current_r = 0, current_g = 0, current_b = 0;
volatile float current_h = 0.0, current_s = 0.0, current_v = 0.0;
volatile uint8_t bg_r = 120, bg_g = 150, bg_b = 115;
volatile bool calibrated = false;
volatile int roi_x = 130, roi_y = 90;
volatile float current_sharpness = 0.0;
volatile int current_led_val = 0;
volatile uint32_t current_frame = 0; 

String current_label = "TRỐNG";
String lastSsidTried = "Connecting...";

void takeSnapshot();

// Giao diện Web
const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cài Đặt WiFi - ESP32-CAM</title>
    <style>
        body {
            background-color: #121212;
            color: #ffffff;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            text-align: center;
            padding-top: 50px;
            margin: 0;
        }
        .container {
            max-width: 400px;
            margin: 0 auto;
            background: #1e1e1e;
            padding: 30px;
            border-radius: 12px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.5);
        }
        h2 {
            color: #00ff66;
            margin-bottom: 25px;
        }
        input {
            width: 90%;
            padding: 12px;
            margin: 10px 0;
            border-radius: 8px;
            border: 1px solid #333;
            background: #2a2a2a;
            color: #fff;
            font-size: 16px;
        }
        button {
            width: 96%;
            padding: 12px;
            margin-top: 15px;
            border-radius: 8px;
            border: none;
            background-color: #00ff66;
            color: #121212;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: 0.2s;
        }
        button:hover {
            background-color: #00cc55;
        }
        .btn-view {
            background-color: #007aff;
            color: white;
        }
        .btn-view:hover {
            background-color: #005bb5;
        }
        #wlist {
            text-align: left;
            margin-top: 20px;
            font-size: 14px;
            color: #00ff66;
        }
        #status_msg {
            color: #ff3b30;
            font-weight: bold;
            margin: 15px 0 5px 0;
            font-size: 14px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>CẤU HÌNH WIFI</h2>
        <form action="/save">
            <input type="text" name="s" placeholder="Tên WiFi (SSID)" required>
            <br>
            <input type="password" name="p" placeholder="Mật khẩu WiFi" required>
            <br>
            <button type="submit">LƯU & KHỞI ĐỘNG LẠI</button>
        </form>
        <br>
        <hr style="border-color: #333;">
        <button type="button" class="btn-view" onclick="loadSavedWifi()">XEM CÁC MẠNG ĐÃ LƯU</button>
        <div id="status_msg"></div>
        <div id="wlist"></div>
    </div>

    <script>
        function loadSavedWifi() {
            fetch('/wifi_list')
                .then(response => response.json())
                .then(data => {
                    let html = '<h3 style="color:#aaa;">Danh sách mạng đã lưu:</h3>';
                    if(data.length === 0) {
                        html += '<p><i>Chưa có mạng nào được lưu</i></p>';
                    } else {
                        data.forEach((wifi, index) => {
                            html += `<p style="margin: 15px 0; padding: 10px; background: #222; border-radius: 8px; display: flex; justify-content: space-between; align-items: center;">
                                <span>
                                    <b>${index + 1}. ${wifi.ssid}</b><br>
                                    <span style="color:#888; font-size:12px;">Mật khẩu: ${wifi.pass}</span>
                                </span>
                                <button type="button" style="width:auto; padding:6px 12px; margin-top:0; background-color:#ff3b30; color:#fff; font-size:12px;" onclick="deleteWifi(${index})">XÓA</button>
                            </p>`;
                        });
                    }
                    document.getElementById('wlist').innerHTML = html;
                });
        }

        function deleteWifi(index) {
            let msgBox = document.getElementById('status_msg');
            msgBox.innerText = "Đang thực hiện xóa mạng...";
            fetch(`/delete_wifi?idx=${index}`)
                .then(response => response.text())
                .then(data => {
                    msgBox.innerText = data;
                    loadSavedWifi();
                    setTimeout(() => { msgBox.innerText = ""; }, 3000);
                });
        }
    </script>
</body>
</html>
)rawliteral";

// GIAO DIỆN WEB GIÁM SÁT REALTIME

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-CAM Phân Loại Màu Tự Động</title>
    <style>
        body {
            background-color: #121212;
            color: #ffffff;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            text-align: center;
            margin: 0;
            padding: 20px;
        }
        h2 {
            color: #00ff66;
            margin-bottom: 20px;
        }
        .dashboard {
            display: flex;
            justify-content: center;
            gap: 20px;
            flex-wrap: wrap;
            max-width: 1200px;
            margin: 0 auto;
        }
        .card {
            background: #1e1e1e;
            border: 2px solid #2d2d2d;
            border-radius: 12px;
            padding: 20px;
            flex: 1;
            min-width: 280px;
            box-shadow: 0 4px 10px rgba(0,0,0,0.3);
        }
        .container {
            position: relative;
            display: inline-block;
            border: 4px solid #1f1f1f;
            border-radius: 8px;
            overflow: hidden;
            background-color: #000;
        }
        #stream_img {
            width: 320px;
            height: 240px;
            display: block;
            cursor: crosshair;
        }
        .roi-box {
            position: absolute;
            width: 60px;
            height: 60px;
            border: 2px solid #00ff66;
            box-sizing: border-box;
            pointer-events: none;
            left: 130px;
            top: 90px;
        }
        .color-preview {
            font-size: 24px;
            font-weight: bold;
            padding: 15px;
            border-radius: 8px;
            margin: 15px auto;
            width: 80%;
            transition: 0.3s;
        }
        .bg-red {
            background-color: #ff3b30;
            color: #ffffff;
            box-shadow: 0 0 10px rgba(255, 59, 48, 0.5);
        }
        .bg-blue {
            background-color: #007aff;
            color: #ffffff;
            box-shadow: 0 0 10px rgba(0, 122, 255, 0.5);
        }
        .bg-gray {
            background-color: #3a3a3c;
            color: #aaaaaa;
        }
        .color-block {
            width: 100px;
            height: 100px;
            margin: 0 auto 15px auto;
            border-radius: 12px;
            border: 3px solid #555;
            transition: background-color 0.2s;
        }
        .sys-text {
            font-family: 'Courier New', Courier, monospace;
            color: #00ff66;
            font-size: 18px;
            font-weight: bold;
        }
        .btn-capture {
            padding: 12px;
            font-size: 16px;
            width: 100%;
            background-color: #007aff;
            color: white;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            font-weight: bold;
            transition: 0.2s;
        }
        .btn-capture:hover {
            background-color: #005bb5;
        }
        .btn-small {
            padding: 10px;
            font-size: 13px;
            background-color: #333;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            width: 100%;
            margin-top: 15px;
            transition: 0.2s;
        }
        .btn-small:hover {
            background-color: #444;
        }
    </style>
</head>
<body>
    <h2>HỆ THỐNG PHÂN LOẠI MÀU</h2>
    
    <div class="dashboard">
        <div class="card">
            <button id="capture_btn" class="btn-capture">CHỤP THỦ CÔNG</button>
            <h3>Kết Quả Phân Loại</h3>
            <div id="color_text_box" class="color-preview bg-gray">TRỐNG</div>
            
            <button id="calib_btn" class="btn-capture" style="background-color: #333; font-size:14px; margin-top:10px;">LƯU MÀU NỀN BĂNG CHUYỀN</button>
            <p id="calib_info" style="font-size: 12px; color: #888;">Đang sử dụng màu nền mặc định.</p>
            
            <hr style="border-color: #333; margin: 20px 0;">
            <p>Điều Chỉnh Đèn Rọi:</p>
            <input type="range" id="led_slider" min="0" max="255" value="0" style="width: 100%; cursor: pointer;">
            
            <button id="wifi_list_btn" class="btn-small">XEM DANH SÁCH WIFI TRONG BỘ NHỚ</button>
            <div id="wifi_list_view" style="font-size: 12px; color:#00ff66; text-align:left; line-height: 1.5;"></div>
        </div>

        <div class="card" style="flex: 1.5;">
            <h3>Hình Ảnh Camera</h3>
            <div class="container" id="img_container">
                <img id="stream_img" src="/jpg">
                <div class="roi-box" id="roi_element"></div>
            </div>
            <p id="sharp_text" style="color: #aaa; font-size: 14px; margin-top: 15px;">Độ sắc nét: 0.0</p>
            <p id="frame_counter" style="color: #00ff66; font-size: 12px;">Tổng số khung hình: 0</p>
            <p style="font-size: 11px; color:#555;">* Click chuột lên vị trí bất kỳ trên ảnh để dời ô quét màu (ROI)</p>
        </div>

        <div class="card">
            <h3>Thông Số Màu Quét</h3>
            <div id="color_block" class="color-block"></div>
            <p id="rgb_values" class="sys-text">R: 0, G: 0, B: 0</p>
            <p id="hsv_values" style="color: #aaa; font-family: monospace; font-size: 14px; line-height: 1.8;">
                H: 0.0° <br> S: 0.0% <br> V: 0.0%
            </p>
        </div>
    </div>

    <script>
        const img = document.getElementById('stream_img');
        const roi = document.getElementById('roi_element');
        let local_frame_count = -1;

        document.getElementById('img_container').onclick = (e) => {
            const rect = img.getBoundingClientRect();
            let x = Math.round((e.clientX - rect.left) / rect.width * 320 - 30);
            let y = Math.round((e.clientY - rect.top) / rect.height * 240 - 30);
            
            x = Math.max(0, Math.min(x, 260));
            y = Math.max(0, Math.min(y, 180));
            
            roi.style.left = x + 'px';
            roi.style.top = y + 'px';
            fetch(`/set_roi?x=${x}&y=${y}`);
        };

        async function updateData() {
            try {
                let res = await fetch('/color');
                let data = await res.json();
                
                document.getElementById('color_block').style.backgroundColor = `rgb(${data.r},${data.g},${data.b})`;
                document.getElementById('rgb_values').innerText = `R: ${data.r}, G: ${data.g}, B: ${data.b}`;
                document.getElementById('hsv_values').innerHTML = `H: ${data.h.toFixed(1)}° <br> S: ${data.s.toFixed(1)}% <br> V: ${data.v.toFixed(1)}%`;
                
                document.getElementById('sharp_text').innerText = `Độ sắc nét: ${data.sharp.toFixed(1)}`;
                document.getElementById('frame_counter').innerText = `Tổng số khung hình: ${data.frame}`;
                
                roi.style.left = data.roi_x + 'px';
                roi.style.top = data.roi_y + 'px';
                
                let box = document.getElementById('color_text_box');
                box.innerText = data.label;
                box.className = "color-preview";
                if (data.label === "VẬT ĐỎ") {
                    box.classList.add("bg-red");
                } else if (data.label === "VẬT XANH DƯƠNG") {
                    box.classList.add("bg-blue");
                } else {
                    box.classList.add("bg-gray");
                }
                
                if (data.frame !== local_frame_count) {
                    local_frame_count = data.frame;
                    img.src = '/jpg?t=' + Date.now();
                }
            } catch (e) {
                console.log("Lỗi đồng bộ dữ liệu màu.");
            }
            setTimeout(updateData, 200);
        }

        updateData();

        document.getElementById('capture_btn').onclick = () => fetch('/capture');
        
        document.getElementById('calib_btn').onclick = () => {
            fetch('/calibrate');
            document.getElementById('calib_info').innerText = "Đã lưu màu nền băng chuyền hiện tại!";
        };
        
        document.getElementById('led_slider').oninput = function() {
            fetch(`/led?val=${this.value}`);
        };
        
        document.getElementById('wifi_list_btn').onclick = () => {
            loadSavedWifi();
        };

        function loadSavedWifi() {
            fetch('/wifi_list')
                .then(r => r.json())
                .then(d => {
                    let h = '<br><b>Các mạng đã lưu:</b><br>';
                    if (d.length === 0) {
                        h += "<i>Chưa có mạng nào</i>";
                    } else {
                        d.forEach((w, i) => {
                            h += `<div style="margin: 8px 0; padding: 8px; background: #2d2d2d; border-radius: 6px; display: flex; justify-content: space-between; align-items: center; text-align: left;">
                                <span>
                                    ${i+1}. <b>${w.ssid}</b><br>
                                    <span style="color:#888; font-size:11px;">Mật khẩu: ${w.pass}</span>
                                </span>
                                <button type="button" style="padding: 4px 8px; background: #ff3b30; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 11px;" onclick="deleteWifi(${i})">Xóa</button>
                            </div>`;
                        });
                    }
                    document.getElementById('wifi_list_view').innerHTML = h;
                });
        }

        function deleteWifi(index) {
            let view = document.getElementById('wifi_list_view');
            view.innerHTML = "<br><i>Đang xóa mạng...</i>";
            fetch(`/delete_wifi?idx=${index}`)
                .then(r => r.text())
                .then(data => {
                    loadSavedWifi();
                });
        }
    </script>
</body>
</html>
)rawliteral";

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

  for (int y = roi_y; y < roi_y + 60; y++) {
    for (int x = roi_x; x < roi_x + 60; x++) {
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

  current_r = sumR / 3600; 
  current_g = sumG / 3600; 
  current_b = sumB / 3600;
  current_sharpness = (float)sharp / 3600;
  
  float t_h = 0, t_s = 0, t_v = 0;
  rgbToHsv(current_r, current_g, current_b, t_h, t_s, t_v);
  current_h = t_h; 
  current_s = t_s; 
  current_v = t_v;

  int bg_diff = abs(current_r - bg_r) + abs(current_g - bg_g) + abs(current_b - bg_b);
  
  if (bg_diff < 40) {
    current_label = "TRỐNG"; 
  } else if (current_v > 25.0f && current_s > 35.0f) {
    if (current_h < 25.0f || current_h > 330.0f) {
      current_label = "VẬT ĐỎ";
    } 
    else if (current_h >= 170.0f && current_h <= 260.0f) {
      current_label = "VẬT XANH DƯƠNG";
    } 
    else {
      current_label = "TRỐNG"; 
    }
  } else {
    current_label = "TRỐNG";
  }
}

void takeSnapshot() {
  if (isConfigMode) return; 
  camera_fb_t * fb = esp_camera_fb_get(); 
  if (!fb) return;

  processColorLogic(fb); 

  if (xSemaphoreTake(frame_mutex, portMAX_DELAY) == pdTRUE) {
    uint8_t *out_jpg = NULL;
    size_t out_jpg_len = 0;
    // Giữ chất lượng nén ảnh ở mức 80 để chống vỡ hạt nhòe nhoẹt
    if (fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 80, &out_jpg, &out_jpg_len)) {
      memcpy(shared_buf, out_jpg, out_jpg_len);
      shared_len = out_jpg_len;
      free(out_jpg); 
    }
    xSemaphoreGive(frame_mutex); 
  }
  esp_camera_fb_return(fb); 
  current_frame++;

  if (current_label == "VẬT ĐỎ") Serial1.println("COLOR:RED");
  else if (current_label == "VẬT XANH DƯƠNG") Serial1.println("COLOR:BLUE");
  else Serial1.println("COLOR:EMPTY");
}

// REST API HANDLERS (WEB SERVER Link)

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

esp_err_t set_roi_handler(httpd_req_t *req) {
  char q[64], x_s[10], y_s[10];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "x", x_s, sizeof(x_s)) == ESP_OK) roi_x = constrain(atoi(x_s), 0, 260);
    if (httpd_query_key_value(q, "y", y_s, sizeof(y_s)) == ESP_OK) roi_y = constrain(atoi(y_s), 0, 180);
  }
  return httpd_resp_send(req, "OK", 2);
}

esp_err_t led_handler(httpd_req_t *req) {
  char q[64], val_s[10];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "val", val_s, sizeof(val_s)) == ESP_OK) { 
      current_led_val = constrain(atoi(val_s), 0, 255); 
      analogWrite(4, current_led_val); 
    }
  }
  return httpd_resp_send(req, "OK", 2);
}

// Setup

void setup() {
  setCpuFrequencyMhz(160); 
  Serial.begin(115200);
  Serial.println("\n>>> KHOI DONG HE THONG ESP32-CAM...");

  Serial1.begin(115200, SERIAL_8N1, 14, 15);

  pinMode(IR_SENSOR_PIN, INPUT_PULLUP); 
  pinMode(4, OUTPUT); analogWrite(4, 0);   
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
    return;
  }
  Serial.println(">>> Camera OK! Dang can bang trang (15 frames)...");
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
    isConfigMode = true; 
    Serial.print(">>> Ket noi WiFi 'ESP32-CAM-SETUP' -> vao web: http://");
    Serial.println(WiFi.softAPIP());

    Serial1.println("WIFI:ERR:ESP32-CAM-SETUP:" + WiFi.softAPIP().toString());
  } else {
    Serial.print(">>> KET NOI THANH CONG! IP cua ban la: http://");
    Serial.println(WiFi.localIP());

    Serial1.println("WIFI:IP:" + WiFi.SSID() + ":" + WiFi.localIP().toString());
    takeSnapshot(); 
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
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/led", HTTP_GET, led_handler, NULL });
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/calibrate", HTTP_GET, calibrate_handler, NULL });
      httpd_register_uri_handler(camera_httpd, new httpd_uri_t{ "/set_roi", HTTP_GET, set_roi_handler, NULL });
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