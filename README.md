# NHA-NAM-V2 — Máy đo chất lượng không khí ESP32

Tổng hợp toàn bộ dự án: thiết bị đo không khí (nhiệt độ, độ ẩm, CO₂, cường độ ánh sáng) dùng **ESP32 DevKit (WROOM-32)**, màn hình TFT cảm ứng 2.4" 240×320, gửi dữ liệu lên cloud qua **MQTT/ThingsBoard**, hỗ trợ **OTA firmware** từ xa và **bảo hành 1 năm** kích hoạt tự động.

- **Firmware version:** `1.0.3` (định nghĩa trong `include/thingsboard_client.h`)
- **Hardware ID thiết bị:** `SJ3VIQQL8A` (`TB_HW_ID`)

---

## 1. Thông số hệ thống

| Thành phần | Chi tiết |
|---|---|
| MCU | ESP32 DevKit (esp32dev), Arduino framework, PlatformIO build |
| Màn hình | ST7789 240×320 portrait, driver TFT_eSPI, SPI 55 MHz |
| Cảm ứng | XPT2046 resistive, SPI riêng (VSPI soft-pins) |
| Cảm biến | SCD30 (CO₂/temp/humi), SHT30 (temp/humi), BH1750 (lux) — cùng bus I2C |
| Kết nối | WiFi (WiFiManager), MQTT (PubSubClient), ThingsBoard MQTT |
| OTA | Tải firmware từ URL theo `hw_id` + version, lệnh qua MQTT topic `ota` |
| Giao diện | 5 trang: Dashboard, WiFi, Zalo, MQTT, Info; bàn phím ảo on-screen |
| Ngôn ngữ log | Tiếng Việt/Anh qua Serial 115200 |

---

## 2. Phần cứng — sơ đồ chân

### I2C (cảm biến)
| Chân | GPIO |
|---|---|
| SDA | 27 |
| SCL | 22 |

Cảm biến: SCD30 + SHT30 + BH1750 treo chung bus I2C. Mỗi cảm biến có flag `xxxAvailable` nếu probe thất bại khi boot → thiết bị vẫn chạy với phần cảm biến còn lại.

### Màn hình TFT (ST7789, cấu hình trong `platformio.ini`)
| Chân | GPIO |
|---|---|
| MOSI | 13 |
| SCLK | 14 |
| CS | 15 |
| DC | 2 |
| RST | -1 (không dùng) |
| Backlight | 21 (HIGH = bật, dùng cho timeout tắt màn hình 1 tiếng) |

### Cảm ứng XPT2046 (định nghĩa trong `src/main.cpp`)
| Chân | GPIO |
|---|---|
| IRQ | 36 |
| MOSI | 32 |
| MISO | 39 |
| CLK | 25 |
| CS | 33 |

Calibration (portrait): `TS_X_MIN=633, TS_X_MAX=3375, TS_Y_MIN=517, TS_Y_MAX=3593`, ngưỡng z ≥ 150.

### Nút bấm
| Nút | GPIO | Chức năng |
|---|---|---|
| BOOT | 0 | Hold 3s → pairing mode (claim ThingsBoard); hold 6s → WiFi config portal; hold 10s + tap 3 lần → reset bảo hành |

---

## 3. Cấu trúc thư mục

```
NHA-NAM-V2/
├── platformio.ini          ← Cấu hình build + build_flags TFT_eSPI
├── rename_firmware.py      ← Post-build: copy firmware.bin → <TB_HW_ID>.bin
├── src/                    ← Source từng module
│   ├── main.cpp            ← Entry: init peripheral, UI, loop chính (sensor/touch/cloud/watchdog)
│   ├── wifi_manager.cpp    ← WiFi STA + config portal (WiFiManager) + MQTT params trong NVS
│   ├── mqtt_client.cpp     ← 2 MQTT client: MQTT-DEV (server cty) + MQTT-USER (tùy chọn người dùng)
│   ├── thingsboard_client.cpp ← Claim token qua HTTP → connect TB MQTT, publish telemetry
│   ├── ota_manager.cpp     ← OTA update từ URL (check version + download)
│   ├── warranty_manager.cpp← Bảo hành 365 ngày, lưu NVS, kích hoạt khi publish MQTT-DEV OK
│   ├── scd30_sensor.cpp    ← Cảm biến CO₂ SCD30 (thư viện Sensirion), FRC calibrate qua MQTT
│   ├── sht30_sensor.cpp    ← Cảm biến temp/humi SHT30 (ArtronShop)
│   └── bh1750_sensor.cpp   ← Cảm biến ánh sáng BH1750 (lux)
├── include/                ← Header tương ứng từng module
│   ├── ui_config.h         ← Kích thước màn hình, bảng màu RGB565, layout, định nghĩa PAGE_*
│   ├── ui_draw.h           ← Vẽ toàn bộ UI: dashboard 2×2 cards, nav bar, keyboard, QR code
│   ├── ui_keyboard.h       ← Bàn phím ảo on-screen (WiFi/MQTT config)
│   ├── ui_icons.h          ← Icon bitmap
│   ├── logo_fuvitech.h     ← Logo splash screen
│   └── User_Setup.h        ← (dự phòng) TFT_eSPI user setup
├── lib/                    ← Thư viện local (Adafruit_SCD30, BH1750...)
├── tools/
│   └── ui_simulator.html   ← Mô phỏng UI trên trình duyệt (thiết kế layout)
├── docs/
│   ├── CLAUDE.md           ← (tham khảo) pattern kiến trúc từ project Aqua2 STM32
│   └── cloud_send_data_guide.md ← Hướng dẫn gửi dữ liệu lên cloud
└── test/                   ← (trống, chưa có unit test)
```

**Quy ước code:** 1 chức năng = 1 cặp `module.cpp` + `module.h`; hàm đặt tên `<module><Action>()` (vd `mqtt_init()`, `tb_connect()`, `scd30_readData()`, `wifi_isConnected()`).

---

## 4. Luồng hoạt động chính (`src/main.cpp`)

### setup()
1. Serial 115200, I2C `Wire.begin(27, 22)`, SPI cảm ứng VSPI
2. Probe 3 cảm biến → flag `scd30Available / sht30Available / bh1750Available`
3. Khởi tạo màn hình + cảm ứng, splash logo
4. `wifi_init()` — connect WiFi đã lưu (NVS), tối đa chờ 5s, thất bại → offline mode
5. NTP: `configTime(GMT+7, "pool.ntp.org", "time.nist.gov")`
6. `warranty_init()` (load trạng thái bảo hành) + `tb_init()` + `tb_connect()` — tự động **claim** access token qua HTTP `gw.fuvitech.vn` nếu chưa có
7. `mqtt_init()` + `mqtt_connect()` nếu có WiFi

### loop()
| Việc | Chu kỳ | Ghi chú |
|---|---|---|
| Đọc cảm biến | 2s | Cập nhật dashboard ngay khi có data mới |
| Publish cloud | 10s | ThingsBoard + MQTT-DEV + MQTT-USER (nếu bật) |
| WiFi watchdog | 5s | Detect mất/mất lại WiFi, reconnect TB |
| Touch | debounce 150ms | Điều hướng page, bàn phím, các nút |
| Tắt màn hình | 1 tiếng không hoạt động | Backlight GPIO 21 OFF, chạm để đánh thức |
| MQTT keep-alive | mỗi vòng lặp | `tb_loop()`, `mqtt_loop()` |

### Dữ liệu publish (JSON, giống nhau cho MQTT và ThingsBoard)
```json
{
  "id_device": "SJ3VIQQL8A",
  "temperature": 28.5,
  "humidity": 65.2,
  "co2": 812,
  "light": 120,
  "device": "ESP32-AirMonitor",
  "timestamp": 1725345600
}
```

---

## 5. Kết nối cloud

### 5.1. MQTT-DEV (server công ty — bắt buộc)
- Broker: `mqtt.fuvitech.vn:1883` (`mqtt_client.h`)
- Topic publish: `maydokhongkhi/<TB_HW_ID>/data`
- Topic subscribe: `maydokhongkhi/<TB_HW_ID>/ota` và `.../cmd`
- Lệnh nhận qua `cmd`:
  - `{"action":"ota","url":"..."}` → bắt đầu OTA update
  - `{"action":"calibrate_co2","ppm":400}` → force calibration SCD30 (FRC)
  - `{"action":"set_ctrl_id","ctrl_id":"FVACTL002"}` → đổi ID board điều khiển ESP-NOW (lưu NVS, xóa MAC cũ, re-subscribe topic `bodieukhienfuviair/<ctrl_id>/MACAdd`)
- Lần đầu publish MQTT-DEV thành công → `warranty_startIfNeeded()` kích hoạt bảo hành

### 5.2. MQTT-USER (broker riêng của người dùng — tùy chọn)
- Cấu hình qua UI (bàn phím ảo): server / port / username / password / topic
- Lưu trong NVS namespace `user-mqtt`, bật/tắt bằng `mqtt_setUserEnabled()`

### 5.3. ThingsBoard
- Server: `tb1.fuvitech.vn:1883`
- **Claim token:** HTTP GET `gw.fuvitech.vn/api/v1/device-heartbeat/?...` → nhận `access_token`, lưu NVS (`tb-config`). Pairing mode: hold BOOT 3s mở cửa sổ claim 5 phút
- Publish telemetry keys: `temp_c`, `humidity`, `co2`, `lux`

### 5.4. OTA (`ota_manager`)
- Chủ động: lệnh MQTT `{"action":"ota","url":"..."}`
- Tự động check: `GET <base_url>?id=<hw_id>&v=<version>` → HTTP 304 = chưa có bản mới, 200 = tải và update, 404 = không có firmware cho device

---

## 6. Giao diện người dùng

- **Dashboard** (`PAGE_DASHBOARD`): 4 card 2×2 hiển thị CO₂, nhiệt độ, độ ẩm, ánh sáng + topbar đồng hồ (NTP, cập nhật mỗi phút)
- **WiFi** (`PAGE_WIFI`): trạng thái kết nối, IP, SSID; bàn phím ảo nhập SSID/pass; nút mở config portal (WiFiManager AP `192.168.4.1`)
- **Zalo** (`PAGE_ZALO`): QR code (`i=<TB_HW_ID>&k=<TB_SECRET_KEY>`) để user scan link thiết bị với Zalo Mini App
- **MQTT** (`PAGE_MQTT`): cấu hình MQTT-USER (host/port/user/pass/topic, enable)
- **Info** (`PAGE_INFO`): thông tin thiết bị, firmware version, trạng thái bảo hành (số ngày còn lại)

---

## 7. Lưu trữ NVS (Preferences)

| Namespace | Key | Nội dung |
|---|---|---|
| `wifi-config` | `ssid`, `pass` | WiFi credentials |
| `mqtt-config` | `server`, `port`, `topic`, `device_id` | Config MQTT-DEV + Device ID (dùng trong config portal) |
| `user-mqtt` | `enabled`, `server`, `port`, `username`, `password`, `topic` | MQTT-USER |
| `tb-config` | `access_token`, `hw_id` | Token ThingsBoard sau claim |
| `espnow` | `ctrl_mac`, `ctrl_id` | MAC board điều khiển + controller ID (ESP-NOW) |
| warranty | (trong `warranty_manager.cpp`) | Timestamp kích hoạt bảo hành |

---

## 8. Build & nạp firmware

### Yêu cầu
- [PlatformIO](https://platformio.org/) (VSCode extension hoặc CLI)
- Python (cho post-build script)

### Lệnh
```bash
pio run                 # Build → .pio/build/esp32dev/SJ3VIQQL8A.bin
pio run -t upload       # Build + nạp qua USB
pio device monitor      # Xem log serial 115200
```

### Post-build (`rename_firmware.py`)
Tự động đọc `TB_HW_ID` từ `include/thingsboard_client.h` và copy `firmware.bin` thành `<TB_HW_ID>.bin` — dùng để upload lên server OTA.

### Đổi thiết bị mới (mass-produce)
1. Sửa `TB_HW_ID`, `TB_SECRET_KEY` trong `include/thingsboard_client.h`
2. Build lại — tên file firmware tự đổi theo HW ID

---

## 9. ESP-NOW — fallback khi mất WiFi

Theo chi tiết trong `ESPNOW_SENSOR_GUIDE.md`: khi mất WiFi (hoặc MQTT thất bại), board cảm biến gửi dữ liệu JSON (giống format MQTT) qua **ESP-NOW unicast** sang board điều khiển FuviAir-Control → board ctrl forward sang STM32 qua UART.

- **MAC exchange qua MQTT** (retain, chỉ 1 lần): sensor publish `maydokhongkhi/<sensorId>/MACAdd`, subscribe `bodieukhienfuviair/<ctrlId>/MACAdd`; MAC + `ctrlId` lưu NVS namespace `espnow` (default ctrlId: `FVACTL002`, đổi từ xa qua lệnh `cmd` `{"action":"set_ctrl_id","ctrl_id":"..."}`)
- **Ctrl heartbeat:** ctrl publish MAC định kỳ ~30s; nếu sensor online nhưng >60s không nghe MAC ctrl (`espNowCtrlMacStale`) → gửi ESP-NOW redundancy cho tới khi nhận lại MAC — xử lý trường hợp ctrl mất WiFi một mình
- **Channel fallback = 1** khi mất WiFi (`ESPNOW_FALLBACK_CH`, phải khớp board điều khiển); khi WiFi reconnect → peer channel reset về 0 (theo AP)
- Module: `include/espnow_sender.h` / `src/espnow_sender.cpp`; tích hợp MAC exchange trong `mqtt_client.cpp`, fallback trong `main.cpp`

## 10. Việc chưa làm

- `test/` chưa có unit test
- `include/ui_draw.h.bak` — file backup cũ, có thể dọn
- `docs/CLAUDE.md` — mô tả project *khác* (Aqua2 STM32), chỉ dùng làm tham khảo pattern

---

*Cập nhật: 2026-09-03*
