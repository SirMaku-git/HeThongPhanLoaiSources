#ifndef WEB_PAGES_H
#define WEB_PAGES_H

#include <Arduino.h>

//Cấu hình WiFi
const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cài Đặt WiFi - esp32 Cam</title>
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

// Xem Cam và chụp
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>esp32 Cam Phân Loại Màu Tự Động</title>
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
        }
        .roi-box {
            position: absolute;
            width: 140px;
            height: 140px;
            border: 2px solid #00ff66;
            box-sizing: border-box;
            pointer-events: none;
            left: 90px;
            top: 50px;
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
            transition: 0.2s;
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

#endif