#include "espnow_sender.h"
#include "thingsboard_client.h" // TB_HW_ID (sensorId)
#include "wifi_manager.h"       // wifi_isConnected
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <Preferences.h>

static const char* TAG = "ESPNOW";

static uint8_t ctrlMacAddr[6] = {0};
static bool    ctrlMacKnown   = false;
static char    ctrlId[32]     = ESPNOW_DEFAULT_CTRL_ID;

// Lần cuối nhận MAC ctrl qua MQTT (topic .../MACAdd, kể cả retained).
// Ctrl phải publish MAC định kỳ (heartbeat) — stale quá lâu = ctrl mất WiFi
static unsigned long lastCtrlMacMs = 0;

// Channel strategy khi offline:
// - apChannel:  kênh router lúc WiFi còn kết nối (board ctrl còn WiFi sẽ đứng trên kênh này)
// - fallback 1: kênh cố định khi CẢ 2 board cùng mất WiFi (phải khớp ESPNOW_FALLBACK_CH bên ctrl)
// -> luân phiên 2 kênh cho tới khi kênh nào được ACK thì khóa vào kênh đó
static uint8_t apChannel = 0;
static uint8_t lastSendCh = 0;
static volatile bool lastSendOk = true;
// Số lần send FAIL liên tiếp (mọi đường: fallback + redundancy).
// Redundancy bị gate khi vượt ngưỡng: ctrl unreachable -> không đáng hy sinh WiFi
static volatile uint8_t consecFails = 0;

static void addOrModPeer() {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ctrlMacAddr, 6);
    peer.channel = 0;   // 0 = dùng channel WiFi hiện tại
    peer.encrypt = false;
    if (esp_now_is_peer_exist(ctrlMacAddr)) esp_now_mod_peer(&peer);
    else                                     esp_now_add_peer(&peer);
}

static void onSendCb(const uint8_t* mac, esp_now_send_status_t status) {
    lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
    if (lastSendOk) consecFails = 0;
    else if (consecFails < 0xFF) consecFails++;
    if (!lastSendOk)
        Serial.printf("[%s] send FAIL (ch=%d) to %02X:%02X:%02X:%02X:%02X:%02X\n", TAG, lastSendCh,
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else if (lastSendCh != 0)
        Serial.printf("[%s] send OK (ch=%d)\n", TAG, lastSendCh);
}

static void loadConfig() {
    Preferences prefs;
    prefs.begin("espnow", true);
    ctrlMacKnown = (prefs.getBytes("ctrl_mac", ctrlMacAddr, 6) == 6);
    prefs.getString("ctrl_id", ESPNOW_DEFAULT_CTRL_ID).toCharArray(ctrlId, sizeof(ctrlId));
    apChannel = prefs.getUChar("ap_ch", 0); // kênh router lần kết nối gần nhất
    prefs.end();
}

// Ghi kênh AP vào NVS để dùng lại sau khi reboot trong tình trạng mất WiFi
static void saveApChannel() {
    if (apChannel == 0) return;
    Preferences prefs;
    prefs.begin("espnow", false);
    prefs.putUChar("ap_ch", apChannel);
    prefs.end();
}

// SSID WiFi đã lưu (wifi-config NVS, ghi bởi wifi_manager/keyboard) — dùng để dò kênh
static char wifiSsid[33] = {0};
static bool  scanRunning = false;
static unsigned long lastScanMs = 0;
static constexpr unsigned long SCAN_RETRY_MS = 30000; // dò lại mỗi 30s nếu chưa tìm thấy

static void loadWifiSsid() {
    Preferences prefs;
    prefs.begin("wifi-config", true);
    prefs.getString("ssid", "").toCharArray(wifiSsid, sizeof(wifiSsid));
    prefs.end();
}

void espNowInit() {
    loadConfig();
    loadWifiSsid();

    if (esp_now_init() != ESP_OK) {
        Serial.printf("[%s] Init failed\n", TAG);
        return;
    }
    esp_now_register_send_cb(onSendCb);

    if (ctrlMacKnown) {
        addOrModPeer();
        Serial.printf("[%s] Ready — ctrl MAC loaded from NVS (ctrl=%s)\n", TAG, ctrlId);
    } else {
        Serial.printf("[%s] Ready — chưa biết MAC board điều khiển, chờ exchange qua MQTT\n", TAG);
    }
}

void espNowSaveCtrlMac(const char* macStr, bool isRetained) {
    unsigned int b[6];
    if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        Serial.printf("[%s] MAC không hợp lệ: %s\n", TAG, macStr);
        return;
    }
    for (int i = 0; i < 6; i++) ctrlMacAddr[i] = (uint8_t)b[i];
    ctrlMacKnown = true;
    
    if (!isRetained) {
        lastCtrlMacMs = millis(); // refresh heartbeat ctrl
        consecFails = 0;          // ctrl còn sống -> mở lại gate redundancy
    }

    // Lưu NVS — không mất khi reboot
    Preferences prefs;
    prefs.begin("espnow", false);
    prefs.putBytes("ctrl_mac", ctrlMacAddr, 6);
    prefs.end();

    addOrModPeer();
    Serial.printf("[%s] Ctrl MAC saved: %s (retained=%d)\n", TAG, macStr, isRetained);
}

// True nếu đã có MAC ctrl nhưng quá lâu không nghe ctrl publish MAC qua MQTT
// → ctrl có thể đã mất WiFi (sensor vẫn online) → gọi ESP-NOW redundancy
bool espNowCtrlMacStale(unsigned long timeoutMs) {
    if (!ctrlMacKnown) return false; // chưa có MAC -> không gửi được, chờ exchange
    return (millis() - lastCtrlMacMs > timeoutMs);
}

// Redundancy chỉ đáng giết WiFi khi ESP-NOW thực sự tới được ctrl.
// Quá N lần FAIL liên tiếp (ctrl tắt / không chạy ESP-NOW) -> gate, giữ cloud.
// Có ACK lại (consecFails reset trong onSendCb) hoặc nhận MAC mới -> tự mở lại.
bool espNowRedundancyAllowed() {
    return consecFails < ESPNOW_REDUNDANCY_MAX_FAILS;
}

// Đặt kênh cố định trên chính peer ESP-NOW: khi peer.channel != 0, frame được
// phát đúng kênh đó bất kể STA đang scan/ở kênh khác (tin cậy hơn ép kênh toàn cục)
static void setPeerChannel(uint8_t ch) {
    esp_now_peer_info_t peer = {};
    if (esp_now_get_peer(ctrlMacAddr, &peer) != ESP_OK) {
        addOrModPeer();
        if (esp_now_get_peer(ctrlMacAddr, &peer) != ESP_OK) return;
    }
    if (peer.channel == ch) return;
    peer.channel = ch;
    esp_now_mod_peer(&peer);
}

// Park radio trên kênh cố định khi offline (fix bắt buộc, khớp với board điều khiển):
// 1. Tắt auto-reconnect + disconnect STA -> dừng scan/connect, radio ngừng bị lôi kéo kênh
// 2. delay(200) chờ ổn định
// 3. esp_wifi_set_channel -> radio đứng im trên kênh gửi ESP-NOW
// Lưu ý: dùng WiFi.disconnect() KHÔNG tham số (wifioff=false) — nếu truyền true
// thì radio STA bị tắt hẳn và esp_now_send không phát được nữa
static bool radioParked = false;

static void parkRadioOnChannel(uint8_t ch) {
    uint8_t cur = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&cur, &second);
    if (radioParked && cur == ch) return; // đã park đúng kênh

    if (WiFi.getAutoReconnect()) WiFi.setAutoReconnect(false);
    WiFi.disconnect();            // ngắt STA, giữ radio chạy cho ESP-NOW
    delay(200);                   // chờ stack ổn định trước khi set channel
    esp_err_t err = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    radioParked = true;
    if (err == ESP_OK)
        Serial.printf("[%s] radio parked on ch=%d (was %d)\n", TAG, ch, cur);
    else
        Serial.printf("[%s] park FAILED (ch=%d, err=%d) — radio đang bận scan?\n", TAG, ch, err);
}

// Chọn kênh gửi khi offline:
// - Kênh ỔN ĐỊNH (apChannel hoặc FALLBACK_CH=1 — nơi board ctrl park radio): 1 ACK là lock,
//   bỏ kênh sau SWEEP_FAIL_THRESHOLD lần FAIL liên tiếp
// - Kênh sweep (2..12): ACK có thể chỉ là trúng lúc radio ctrl đi ngang qua khi scan
//   reconnect -> cần 2 ACK liên tiếp mới lock, và chỉ 2 FAIL liên tiếp là bỏ kênh (quét nhanh)
static uint8_t candIdx = 0;     // 0 = apChannel, 1..13 = sweep kênh 1..13
static uint8_t failCount = 0;   // số lần FAIL liên tiếp trên kênh hiện tại
static uint8_t okCount = 0;     // số lần ACK liên tiếp trên kênh sweep
static constexpr uint8_t SWEEP_FAIL_THRESHOLD = 5;

// Board ctrl chỉ park radio ổn định trên 2 kênh: kênh AP của nó (nếu còn WiFi)
// hoặc ESPNOW_FALLBACK_CH (mất WiFi) — khớp macro trong espnow_sender.h / firmware ctrl
static bool isStableCh(uint8_t ch) {
    return ch == apChannel || ch == ESPNOW_FALLBACK_CH;
}

static uint8_t currentCandidate() {
    if (candIdx == 0) return apChannel != 0 ? apChannel : ESPNOW_FALLBACK_CH;
    return candIdx;
}
static void advanceCandidate() {
    candIdx++;
    if (candIdx > 13) candIdx = 1; // hết sweep -> quay lại kênh 1 (ap chỉ thử đầu tiên)
}

static uint8_t pickOfflineChannel() {
    if (lastSendCh != 0 && lastSendOk) {
        if (isStableCh(lastSendCh)) { failCount = 0; okCount = 0; return lastSendCh; } // lock kênh ổn định
        okCount++;
        if (okCount >= 2) { failCount = 0; return lastSendCh; } // 2 ACK liên tiếp -> kênh này có ctrl thật
        return lastSendCh; // ACK lẻ tẻ trên kênh sweep -> thử lại để xác nhận
    }
    okCount = 0;

    if (lastSendCh != 0) {
        failCount++;
        uint8_t threshold = isStableCh(lastSendCh) ? SWEEP_FAIL_THRESHOLD : 2;
        if (failCount < threshold)
            return lastSendCh;
        failCount = 0;
        advanceCandidate();
        if (currentCandidate() == lastSendCh) advanceCandidate(); // bỏ kênh vừa FAIL
    }
    return currentCandidate();
}

void espNowSendSensorData(float temperature, float humidity, float co2, float lux) {
    if (!ctrlMacKnown) {
        Serial.printf("[%s] Chưa có MAC board điều khiển -> bỏ qua\n", TAG);
        return;
    }

    // JSON giữ nguyên format MQTT hiện tại
    char json[200];
    snprintf(json, sizeof(json),
        "{\"id_device\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f,\"co2\":%d,\"light\":%.0f}",
        TB_HW_ID, temperature, humidity, (int)round(co2), lux);

    // Luôn dùng channel sweep kể cả khi WiFi còn (MQTT đã fail mới gọi hàm này).
    // Controller có thể đang ở apChannel (còn WiFi) hoặc FALLBACK_CH=1 (mất WiFi).
    // Sweep: apChannel trước → 1..13 → tìm được controller ở bất kỳ trạng thái nào.
    // parkRadioOnChannel() disconnect WiFi nếu cần; espnow_loop() reconnect lại sau 60s.
    uint8_t ch = pickOfflineChannel();
    parkRadioOnChannel(ch);
    setPeerChannel(ch);
    lastSendCh = ch;
    Serial.printf("[%s] offline, send on ch=%d (ap=%d)\n", TAG, ch, apChannel);

    esp_err_t result = esp_now_send(ctrlMacAddr, (const uint8_t*)json, strlen(json));
    if (result == ESP_OK) Serial.printf("[%s] Send queued: %s\n", TAG, json);
    else                  Serial.printf("[%s] Send failed: %d\n", TAG, result);
}


void espNowOnWifiConnected() {
    if (WiFi.isConnected() && WiFi.channel() != apChannel) {
        apChannel = WiFi.channel();
        saveApChannel();
        Serial.printf("[%s] saved AP channel %d\n", TAG, apChannel);
    }
}

// Offline: (1) thử kết nối lại WiFi định kỳ — park radio làm mất auto-reconnect
// nên phải chủ động retry; (2) dò kênh router bằng scan nếu chưa biết apChannel
static constexpr unsigned long WIFI_RETRY_MS  = 60000;
static unsigned long lastWifiRetryMs = 0;

void espnow_loop() {
    if (wifi_isConnected()) {
        radioParked = false;
        return;
    }

    unsigned long now = millis();

    // Thử WiFi lại mỗi 60s: bỏ park, bật auto-reconnect, WiFi.begin() với config đã lưu.
    // Radio sẽ rời kênh park trong lúc connect — lần gửi ESP-NOW sau park lại.
    if (now - lastWifiRetryMs >= WIFI_RETRY_MS) {
        lastWifiRetryMs = now;
        radioParked = false;
        WiFi.setAutoReconnect(true);
        WiFi.begin();
        Serial.printf("[%s] retry WiFi connect...\n", TAG);
        return;
    }

    // Dò kênh router bằng scan async khi chưa biết apChannel
    if (apChannel != 0 || wifiSsid[0] == '\0') return;

    if (!scanRunning) {
        if (now - lastScanMs < SCAN_RETRY_MS) return;
        lastScanMs = now;
        if (WiFi.scanNetworks(true /*async*/) == WIFI_SCAN_RUNNING) {
            scanRunning = true;
            Serial.printf("[%s] scanning for AP \"%s\"...\n", TAG, wifiSsid);
        }
        return;
    }

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;   // đang scan
    scanRunning = false;
    if (n < 0) return;                    // failed -> thử lại sau 30s

    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == wifiSsid) {
            apChannel = WiFi.channel(i);
            saveApChannel();
            candIdx = 0;   // đã biết kênh AP -> sweep ưu tiên lại từ kênh AP
            Serial.printf("[%s] found AP \"%s\" on channel %d\n", TAG, wifiSsid, apChannel);
            break;
        }
    }
    if (apChannel == 0)
        Serial.printf("[%s] AP \"%s\" not found in %d networks\n", TAG, wifiSsid, n);
    WiFi.scanDelete();
}

void espNowOnWifiReconnected() {
    lastSendCh = 0;
    lastSendOk = true;
    candIdx = 0;      // reset sweep, ưu tiên lại kênh AP
    failCount = 0;
    okCount = 0;
    radioParked = false;
    WiFi.setAutoReconnect(true);
    espNowOnWifiConnected();
    if (!ctrlMacKnown) return;
    esp_now_peer_info_t peer = {};
    if (esp_now_get_peer(ctrlMacAddr, &peer) == ESP_OK && peer.channel != 0) {
        peer.channel = 0;   // quay lại theo channel AP
        esp_now_mod_peer(&peer);
        Serial.printf("[%s] peer channel reset -> 0 (theo AP)\n", TAG);
    }
}

const char* espNowGetCtrlId() { return ctrlId; }

void espNowSetCtrlId(const char* id) {
    strncpy(ctrlId, id, sizeof(ctrlId) - 1);
    ctrlId[sizeof(ctrlId) - 1] = '\0';

    // Đổi controller -> MAC cũ không còn hợp lệ, xóa để chờ exchange lại
    // (board ctrl mới publish MAC retain trên topic mới, nhận được ngay khi subscribe)
    ctrlMacKnown = false;
    memset(ctrlMacAddr, 0, sizeof(ctrlMacAddr));

    Preferences prefs;
    prefs.begin("espnow", false);
    prefs.putString("ctrl_id", ctrlId);
    prefs.remove("ctrl_mac");
    prefs.end();

    Serial.printf("[%s] ctrlId saved: %s (old ctrl MAC cleared)\n", TAG, ctrlId);
}
