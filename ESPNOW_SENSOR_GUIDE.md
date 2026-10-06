# Hướng dẫn ESP-NOW đầy đủ — Board cảm biến NHA-NAM-V2

> Tài liệu này mô tả **chính xác code hiện tại** (`src/espnow_sender.cpp`, `src/mqtt_client.cpp`, `src/main.cpp`), thay cho bản thiết kế cũ.

---

## 1. Mục đích

Khi **mất WiFi** (hoặc WiFi còn nhưng **mất internet** → MQTT fail), board cảm biến vẫn gửi được dữ liệu sang **board điều khiển FuviAir-Control** qua **ESP-NOW unicast** (peer-to-peer, không cần internet). Board điều khiển forward dữ liệu sang STM32 qua UART dạng `<[SENSOR]{json}>` — STM32 không cần thay đổi gì.

```
Khi có WiFi:
  [Sensor] ──MQTT──► Internet ──MQTT──► [Ctrl ESP32] ──UART──► STM32

Khi mất WiFi / mất internet:
  [Sensor] ──ESP-NOW unicast──────────► [Ctrl ESP32] ──UART──► STM32
```

**Format JSON gửi qua ESP-NOW — giữ nguyên format MQTT:**

```json
{
  "id_device": "SJ3VIQQL8A",
  "temperature": 28.5,
  "humidity": 65.2,
  "co2": 812,
  "light": 120
}
```

---

## 2. Kiến trúc module

| File | Vai trò |
|---|---|
| `include/espnow_sender.h` | API + `ESPNOW_FALLBACK_CH=1`, `ESPNOW_DEFAULT_CTRL_ID="FVACTL002"` |
| `src/espnow_sender.cpp` | Toàn bộ logic ESP-NOW: peer, MAC NVS, sweep kênh, park radio, scan AP |
| `src/mqtt_client.cpp` | MAC exchange qua MQTT + lệnh `set_ctrl_id` |
| `src/main.cpp` | Tích hợp vào loop: fallback khi publish fail, watchdog WiFi |
| `examples/espnow_receiver/` | Sketch ESP32 test — đứng im kênh 1 để verify board cảm biến gửi được |

### Thứ tự khởi tạo trong `setup()` (quan trọng)

```cpp
espNowInit();   // TRƯỚC mqtt_init — để ctrlId được load từ NVS
mqtt_init();    // dùng espNowGetCtrlId() để build topic subscribe MAC
mqtt_connect();
```

---

## 3. Trao đổi MAC qua MQTT (chỉ 1 lần)

ESP-NOW là unicast → 2 board phải biết MAC của nhau. Khi còn WiFi, 2 board trao đổi MAC qua MQTT với **retain=true** (board reconnect sau vẫn nhận được):

```
Board cảm biến                          Board điều khiển
      │                                        │
      │── PUBLISH maydokhongkhi/SJ3VIQQL8A/MACAdd (retain, MAC của mình)
      │                                        │
      │◄─ SUBSCRIBE bodieukhienfuviair/FVACTL002/MACAdd
      │  (board ctrl publish MAC của nó lên đây, retain)
      │                                        │
   Lưu MAC ctrl vào NVS "espnow"          Lưu MAC sensor vào NVS
   → đăng ký ESP-NOW peer
```

- **Publish** MAC của mình: trong `mqtt_connect()` (`mqtt_client.cpp`) — mỗi lần connect MQTT đều publish lại (retain) để board mới luôn bắt được.
- **Subscribe** topic MAC của ctrl: cũng trong `mqtt_connect()`.
- **Nhận MAC ctrl**: callback `onDevMessage()` → `espNowSaveCtrlMac()` — parse chuỗi `AA:BB:CC:DD:EE:FF`, lưu NVS, thêm/sửa peer.
- MAC lưu NVS → **không mất khi reboot**, exchange chỉ cần 1 lần.

### Đổi board điều khiển từ xa — lệnh `set_ctrl_id`

Không cần build firmware riêng cho mỗi controller. Publish lên topic cmd:

```
Topic:   maydokhongkhi/SJ3VIQQL8A/cmd
Payload: {"action":"set_ctrl_id","ctrl_id":"FVACTLXXX"}
```

Hành vi (`mqtt_client.cpp`):
1. Lưu `ctrl_id` mới vào NVS (`espNowSetCtrlId()`).
2. **Xóa MAC controller cũ** khỏi RAM + NVS (tránh gửi ESP-NOW nhầm board cũ).
3. Rebuild topic `bodieukhienfuviair/<ctrl_id_mới>/MACAdd`, unsubscribe topic cũ, subscribe topic mới ngay lập tức.
4. Board ctrl mới publish MAC retain trên topic mới → board cảm biến nhận lại MAC tự động.

> Mặc định vẫn là `FVACTL002` (`ESPNOW_DEFAULT_CTRL_ID`), dùng khi NVS chưa có giá trị.

### Detect "ctrl mất WiFi một mình" — MAC heartbeat + `espNowCtrlMacStale()`

**Vấn đề:** sensor chỉ biết trạng thái của chính nó. Nếu ctrl mất WiFi mà sensor còn WiFi thì publish MQTT vẫn thành công (broker nhận được) → fallback không bao giờ kích hoạt → **blind spot**.

**Giải pháp đã implement:** MAC của ctrl đóng vai trò **heartbeat**.
- **Bên ctrl (bắt buộc):** publish MAC của mình lên `bodieukhienfuviair/<ctrlId>/MACAdd` **định kỳ ~30 giây** (retain), thay vì chỉ publish 1 lần lúc connect
- **Bên sensor:** mỗi lần nhận MAC qua MQTT → `lastCtrlMacMs = millis()`. Trong chu kỳ publish cloud 10s, nếu publish cloud **thành công** nhưng `espNowCtrlMacStale(60000UL)` == true (quá 60s không nghe MAC ctrl) → gọi `espNowSendSensorData()` gửi redundancy sang ctrl (ctrl đang offline, park radio kênh 1, vẫn nhận được ESP-NOW)
- Khi ctrl có WiFi lại → publish MAC → sensor nhận lại → stale clear → tự động ngừng ESP-NOW, quay về MQTT-only

**Hành vi khi stale (để hiểu rõ):** `espNowSendSensorData()` park radio (disconnect WiFi) → các chu kỳ 10s sau publish cloud fail → đi tiếp đường fallback ESP-NOW như mất internet. `espnow_loop()` retry WiFi mỗi 60s → reconnect → nhận lại retained MAC → stale clear 60s → phát hiện stale lại nếu ctrl vẫn offline. Kết quả: sensor nhấp nhô online/offline ~60s/lần, **board ctrl nhận dữ liệu liên tục** (MQTT khi sensor online + ESP-NOW khi sensor park), cloud chỉ bị gián đoạn lúc park.

**Gate chống flap vô hạn (`espNowRedundancyAllowed()`):** nếu redundancy được kích hoạt mà ctrl **không ACK** (ctrl tắt / ESP-NOW ctrl chưa chạy / MAC sai) thì việc park radio là hy sinh WiFi vô nghĩa → sensor sẽ flap mãi mãi. Sau **3 lần send FAIL liên tiếp** (`ESPNOW_REDUNDANCY_MAX_FAILS`), redundancy bị tắt — sensor giữ WiFi cho cloud. Tự mở lại khi: có send ESP-NOW nào được ACK, hoặc **nhận MAC mới qua MQTT** (ctrl còn sống → reset bộ đếm fail, tránh deadlock gate khoá vĩnh viễn trong khi sensor online).

**Giới hạn đã biết:** retained MAC được broker trả lại **mỗi lần sensor subscribe** — sau fix `devMaintain()` (mqtt_client.cpp), subscribe chỉ chạy 1 lần mỗi connection mới → chỉ trong 60s đầu sau mỗi lần **MQTT reconnect**, stale không được phát hiện. Với ctrl heartbeat 30s thì mọi thứ hội tụ sau tối đa ~1 chu kỳ.

**Bug đã fix liên quan:** trước đây `mqtt_connect()` re-subscribe topic `MACAdd` mỗi chu kỳ publish 10s → broker trả retained MAC liên tục → `lastCtrlMacMs` refresh mãi → stale không bao giờ fire. Nếu thấy log "Subscribed topics" + "Published sensor MAC" lặp mỗi 10s là firmware cũ.

---

## 4. Logic khi offline — kênh & sweep

ESP-NOW chỉ gửi được khi 2 board **cùng kênh WiFi**. Board ctrl park radio trên 1 trong 2 kênh ổn định:
- **`apChannel`**: kênh router của board ctrl (khi ctrl còn WiFi)
- **`ESPNOW_FALLBACK_CH = 1`**: kênh cố định khi cả 2 cùng mất WiFi — **phải khớp firmware board ctrl**

### Chiến lược chọn kênh của board cảm biến (`pickOfflineChannel()`)

```
candIdx = 0 → thử apChannel (kênh router của chính mình, lưu NVS)
candIdx = 1..13 → sweep kênh 1..13 (board ctrl có thể ở bất kỳ đâu)
```

| Tình huống | Điều kiện lock kênh | Điều kiện bỏ kênh |
|---|---|---|
| Kênh ổn định (apChannel hoặc kênh 1) | 1 ACK → lock | 5 FAIL liên tiếp |
| Kênh sweep (2..13) | 2 ACK liên tiếp → lock | 2 FAIL liên tiếp |

ACK = callback `onSendCb` báo `ESP_NOW_SEND_SUCCESS`.

### Park radio (`parkRadioOnChannel`)

Radio ESP32 bị kéo loanh quanh kênh khi STA đang scan/reconnect → ESP-NOW gửi thất bại. Fix:

1. Tắt auto-reconnect + `WiFi.disconnect()` (**không** tham số `true` — tắt hẳn STA là không phát được ESP-NOW)
2. `delay(200)` chờ stack ổn định
3. `esp_wifi_set_channel(ch)` — radio đứng im trên kênh gửi

Kênh được đặt **trên chính peer** (`setPeerChannel`) — tin cậy hơn ép kênh toàn cục.

### Vòng lặp offline (`espnow_loop()`, gọi mỗi vòng `loop()`)

| Việc | Chu kỳ | Ghi chú |
|---|---|---|
| Retry WiFi | 60s | Bỏ park, bật auto-reconnect, `WiFi.begin()` với config đã lưu |
| Scan AP để tìm `apChannel` | 30s (async) | Chỉ khi chưa biết apChannel + có SSID đã lưu trong NVS `wifi-config` |
| Gửi dữ liệu | 10s (cùng chu kỳ publish cloud) | Sweep kênh như bảng trên |

### Khi WiFi có lại (`espNowOnWifiReconnected`)

- Reset toàn bộ trạng thái sweep (candIdx, failCount, okCount)
- Peer channel → 0 (đứng theo kênh AP)
- Bật lại auto-reconnect, lưu apChannel mới

---

## 5. Luồng dữ liệu trong `main.cpp`

```cpp
// Mỗi 10s (chu kỳ publish cloud):
if (wifi_isConnected()) {
    tbOk  = tb_publishSensorData(...);   // ThingsBoard
    mqttOk = mqtt_publishSensorData(...); // MQTT-DEV (server cty) + MQTT-USER
    if (tbOk || mqttOk) → OK, không cần ESP-NOW
    else → "WiFi OK nhưng mất internet" → espNowSendSensorData(...)  // fallback
} else {
    espNowSendSensorData(...);            // mất WiFi → fallback luôn
}
```

Lưu ý: `mqtt_publishSensorData()` trả về `devOk` — MQTT-USER fail **không** tính là mất cloud (tránh fallback + park radio không cần thiết).

Watchdog WiFi mỗi 5s trong `loop()`:
- Mất WiFi → ghi log, update UI. KHÔNG tự mở config portal (tránh block touch).
- WiFi có lại → `espNowOnWifiReconnected()`, reconnect TB, update UI.
- Đang có WiFi → `espNowOnWifiConnected()` lưu kênh AP vào NVS.

---

## 6. NVS namespace `espnow`

| Key | Kiểu | Nội dung |
|---|---|---|
| `ctrl_mac` | bytes[6] | MAC board điều khiển (xóa khi đổi ctrl_id) |
| `ctrl_id` | string | Controller ID, default `FVACTL002` — đổi qua lệnh `set_ctrl_id` |
| `ap_ch` | uchar | Kênh router lần kết nối gần nhất (dùng sau reboot khi offline) |

---

## 7. Test với ESP32 khác (`examples/espnow_receiver/`)

Dùng khi chưa có board điều khiển thật:

1. Nạp sketch vào 1 ESP32 khác (có project PlatformIO riêng kèm theo trong thư mục).
2. Mở serial 115200 — sketch in MAC của nó và đứng im trên **kênh 1**.
3. Cần inject MAC này vào board cảm biến: gửi qua MQTT `bodieukhienfuviair/<ctrl_id>/MACAdd` (payload = chuỗi MAC, **retain=true**), hoặc publish trực tiếp bằng mosquitto:
   ```bash
   mosquitto_pub -h mqtt.fuvitech.vn -p 1883 \
     -t "bodieukhienfuviair/FVACTL002/MACAdd" \
     -m "AA:BB:CC:DD:EE:FF" -r
   ```
4. Rút WiFi board cảm biến → quan sát serial: `[ESPNOW] send OK (ch=1)` và RX trên ESP32 test.

Muốn nghe kênh khác: sửa `ESPNOW_TEST_CH` trong sketch.

---

## 8. Checklist tích hợp board điều khiển (FuviAir-Control)

- [ ] `ESPNOW_FALLBACK_CH` 2 bên **phải khớp** (mặc định 1)
- [ ] Board ctrl park radio trên kênh 1 khi mất WiFi (cùng cơ chế: tắt auto-reconnect → disconnect → set channel)
- [ ] Board ctrl publish MAC của mình lên `bodieukhienfuviair/<ctrlId>/MACAdd` với **retain=true** mỗi lần MQTT connect
- [ ] Board ctrl **publish MAC định kỳ ~30s** (heartbeat) — sensor dùng để detect ctrl mất WiFi (`espNowCtrlMacStale`)
- [ ] Board ctrl subscribe `maydokhongkhi/<sensorId>/MACAdd` để biết MAC sensor (nếu cần)
- [ ] Nhận JSON từ ESP-NOW → forward STM32 qua UART: `<[SENSOR]{json}>`
- [ ] Khi WiFi có lại: peer channel reset về 0 (theo AP)

---

## 9. Troubleshooting

| Triệu chứng | Nguyên nhân / Fix |
|---|---|
| `[ESPNOW] send FAIL` liên tục | Khác kênh — xem log `(ch=X)`, board ctrl có park đúng kênh đó không? `ESPNOW_FALLBACK_CH` đã khớp chưa? |
| `[ESPNOW] Chưa có MAC board điều khiển` | Chưa exchange MAC — board ctrl phải publish retain lên topic `bodieukhienfuviair/<ctrl_id>/MACAdd`, hoặc ctrl_id sai (dùng `set_ctrl_id` để đổi) |
| `park FAILED ... radio đang bận scan?` | Gọi khi radio đang scan/connect — lần gửi sau sẽ park lại, không fatal |
| Reboot trong khi offline không gửi được | `ap_ch` chưa lưu (chưa từng kết nối WiFi nào) → board sẽ scan tìm AP; nếu SSID không có trong vùng → sweep toàn kênh |
| Đổi controller xong vẫn gửi sang board cũ | Ctrl_id đổi nhưng MAC cũ còn — code hiện tại đã tự xóa MAC khi `set_ctrl_id`; nếu gặp lại, kiểm tra firmware có bản mới không |
| ESP-NOW gửi OK nhưng ctrl không nhận | Callback send OK chỉ nghĩa là frame được ACK ở tầng MAC — kiểm tra board ctrl thực sự đăng ký recv callback trên kênh đó |

---

*Cập nhật: 2026-09-09 — khớp với firmware 1.0.3 + tính năng `set_ctrl_id`*
