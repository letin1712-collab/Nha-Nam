#include "thingsboard_client.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ============================================================
// Key mapping
// ============================================================
static constexpr char TEMP_KEY[] = "temp_c";
static constexpr char HUMI_KEY[] = "humidity_percent";
static constexpr char CO2_KEY[]  = "co2_ppm";
static constexpr char LUX_KEY[]  = "light_lux";

// ============================================================
// Internal state
// ============================================================
static char s_accessToken[64] = "";
static WiFiClient   s_wifiClient;
static PubSubClient s_mqttClient(s_wifiClient);

static bool s_pairingMode = false;
static unsigned long s_pairingUntil = 0;

// ============================================================
// NVS helpers — lưu/load token để dùng làm fallback khi offline
// ============================================================
static void _saveTokenToNVS(const char* token) {
    Preferences prefs;
    prefs.begin(TB_NVS_NS, false);
    prefs.putString(TB_NVS_TOKEN, token);
    prefs.putString(TB_NVS_HW_ID, TB_HW_ID);
    prefs.end();
}

static void _loadTokenFromNVS() {
    Preferences prefs;
    prefs.begin(TB_NVS_NS, true);
    String storedHwId = prefs.getString(TB_NVS_HW_ID, "");
    String tok        = prefs.getString(TB_NVS_TOKEN, "");
    prefs.end();

    // Nếu HW_ID thay đổi (flash firmware mới) → token cũ không còn hợp lệ
    if (storedHwId != TB_HW_ID) {
        Serial.printf("[TB] HW_ID changed (%s -> %s), ignoring cached token\n",
                      storedHwId.c_str(), TB_HW_ID);
        tok = "";
    }

    strncpy(s_accessToken, tok.c_str(), sizeof(s_accessToken) - 1);
    s_accessToken[sizeof(s_accessToken) - 1] = '\0';
}

// ============================================================
// tb_init
// Luôn gọi heartbeat để lấy token mới (app quét QR được bất kỳ lúc nào).
// Nếu heartbeat thất bại (offline/server lỗi) → dùng token NVS làm fallback.
// ============================================================
void tb_init() {
    s_mqttClient.setServer(TB_SERVER, TB_PORT);
    s_mqttClient.setKeepAlive(60);

    // CoreIoT: dùng access token cố định từ COREIOT_TOKEN (không claim/NVS)
    strncpy(s_accessToken, COREIOT_TOKEN, sizeof(s_accessToken) - 1);
    s_accessToken[sizeof(s_accessToken) - 1] = '\0';
    Serial.printf("[TB] CoreIoT token: %.6s...\n", s_accessToken);

    Serial.println("[TB] ThingsBoard client initialized");
}

// ============================================================
// tb_claim — HTTP GET heartbeat để lấy token mới
// Luôn được gọi khi boot, kể cả khi đã có token NVS
// ============================================================
bool tb_claim() {
    // CoreIoT dùng token cố định -> tắt claim của Fuvitech (tránh ghi đè token)
    return false;
#if 0
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[TB] Claim skipped: WiFi not connected");
        return false;
    }

    String url = String("http://") + TB_CLAIM_HOST + TB_CLAIM_PATH
               + "?k=" + TB_SECRET_KEY
               + "&i=" + TB_HW_ID;

    Serial.printf("[TB] Heartbeat: %s\n", url.c_str());

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    int httpCode = http.GET();

    if (httpCode < 200 || httpCode > 299) {
        Serial.printf("[TB] Heartbeat HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[TB] Heartbeat HTTP %d: %s\n", httpCode, payload.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[TB] JSON parse error");
        return false;
    }

    const char* token = doc["data"]["access_token"];
    if (!token || strlen(token) == 0) {
        if (httpCode == 202) {
            Serial.println("[TB] Device not yet claimed by user (scan QR on app)");
        } else {
            Serial.println("[TB] No token in response");
        }
        return false;
    }

    // Kiểm tra xem token mới có khác token cũ không
    if (strcmp(s_accessToken, token) != 0) {
        // Token khác -> Cập nhật RAM và NVS, sau đó ngắt kết nối MQTT cũ
        strncpy(s_accessToken, token, sizeof(s_accessToken) - 1);
        s_accessToken[sizeof(s_accessToken) - 1] = '\0';
        _saveTokenToNVS(s_accessToken);
        Serial.printf("[TB] New token received: %.8s...\n", s_accessToken);

        // Ngắt kết nối để tự động reconnect với token mới
        if (s_mqttClient.connected()) {
            s_mqttClient.disconnect();
        }
    } else {
        Serial.println("[TB] Received same token, skipping disconnect.");
    }

    return true;
#endif
}

// ============================================================
// tb_connect
// ============================================================
bool tb_connect() {
    unsigned long now = millis();

    if (s_pairingMode && now > s_pairingUntil) {
        s_pairingMode = false;
        Serial.println("[TB] Pairing window timeout");
    }

    if (s_mqttClient.connected()) return true;

    if (strlen(s_accessToken) == 0) return false;

    Serial.printf("[TB] Connecting to %s:%d ...\n", TB_SERVER, TB_PORT);

    bool connected = s_mqttClient.connect(
        TB_HW_ID,       // clientId
        s_accessToken,  // username = access token
        ""              // password = empty
    );

    if (connected) {
        Serial.println("[TB] MQTT connected!");

    } else {
        int rc = s_mqttClient.state();
        Serial.printf("[TB] MQTT connect failed, rc=%d\n", rc);

        if (rc == MQTT_CONNECT_BAD_CREDENTIALS || rc == MQTT_CONNECT_UNAUTHORIZED) {
            Serial.println("[TB] Token rejected by CoreIoT - kiem tra COREIOT_TOKEN");
        }
    }
    return connected;
}


bool tb_isConnected() {
    return s_mqttClient.connected();
}

static bool _sendTelemetry(float temperature, float humidity, float co2, float lux) {
    JsonDocument doc;
    doc[TEMP_KEY] = round(temperature * 10.0f) / 10.0f;
    doc[HUMI_KEY] = round(humidity    * 10.0f) / 10.0f;
    doc[CO2_KEY]  = (int)round(co2);
    doc[LUX_KEY]  = (int)round(lux);
    char buf[200];
    serializeJson(doc, buf);
    bool ok = s_mqttClient.publish("v1/devices/me/telemetry", buf);
    Serial.printf("[TB] Telemetry %s: %s\n", ok ? "OK" : "FAIL", buf);
    return ok;
}

static bool _sendAttribute(float temperature, float humidity, float co2, float lux) {
    JsonDocument doc;
    doc[TEMP_KEY] = round(temperature * 10.0f) / 10.0f;
    doc[HUMI_KEY] = round(humidity    * 10.0f) / 10.0f;
    doc[CO2_KEY]  = (int)round(co2);
    doc[LUX_KEY]  = (int)round(lux);
    char buf[200];
    serializeJson(doc, buf);
    bool ok = s_mqttClient.publish("v1/devices/me/attributes", buf);
    Serial.printf("[TB] Attribute %s: %s\n", ok ? "OK" : "FAIL", buf);
    return ok;
}


bool tb_publishSensorData(float temperature, float humidity, float co2, float lux) {
    if (!s_mqttClient.connected()) {
        if (!tb_connect()) return false;
    }
    bool t = _sendTelemetry(temperature, humidity, co2, lux);
    bool a = _sendAttribute(temperature, humidity, co2, lux);
    return t && a;
}


void tb_loop() {
    static unsigned long lastClaimAttempt = 0;
    unsigned long now = millis();

    if (s_pairingMode && (lastClaimAttempt == 0 || now - lastClaimAttempt >= 3000UL)) {
        lastClaimAttempt = now;
        tb_claim();
    }

    if (!s_mqttClient.connected()) {
        tb_connect();
    }
    s_mqttClient.loop();
}

const char* tb_getAccessToken() {
    return s_accessToken;
}

void tb_clearToken() {
    Serial.println("[TB] Clearing token (RAM + NVS)");
    s_mqttClient.disconnect();
    s_accessToken[0] = '\0';
    _saveTokenToNVS("");
}

void tb_startPairingWindow(unsigned long durationMs) {
    s_pairingMode = true;
    s_pairingUntil = millis() + durationMs;
    Serial.printf("[TB] Pairing window ON for %lu ms\n", durationMs);
}

bool tb_isPairingMode() {
    if (s_pairingMode && millis() > s_pairingUntil) {
        s_pairingMode = false;
    }
    return s_pairingMode;
}