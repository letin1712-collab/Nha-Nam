#include "mqtt_client.h"
#include "wifi_manager.h"
#include "scd30_sensor.h"
#include "espnow_sender.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

static WiFiClient devWifiClient;
static WiFiClient userWifiClient;
static PubSubClient devClient(devWifiClient);
static PubSubClient userClient(userWifiClient);

static bool user_enabled = false;
static char user_server[64] = "";
static int  user_port = 1883;
static char user_username[40] = "";
static char user_password[40] = "";
static char user_topic[100] = "";

// ESP-NOW MAC exchange (ESPNOW_SENSOR_GUIDE.md)
static String topicMyMac;   // maydokhongkhi/{sensorId}/MACAdd   — publish MAC của mình
static String topicCtrlMac; // bodieukhienfuviair/{ctrlId}/MACAdd — subscribe nhận MAC board điều khiển

static void loadUserConfig() {
    Preferences prefs;
    prefs.begin("user-mqtt", true);
    user_enabled = prefs.getBool("enabled", false);
    prefs.getString("server", "").toCharArray(user_server, sizeof(user_server));
    user_port = prefs.getInt("port", 1883);
    prefs.getString("username", "").toCharArray(user_username, sizeof(user_username));
    prefs.getString("password", "").toCharArray(user_password, sizeof(user_password));
    prefs.getString("topic", "").toCharArray(user_topic, sizeof(user_topic));
    prefs.end();
}

static void saveUserConfig() {
    Preferences prefs;
    prefs.begin("user-mqtt", false);
    prefs.putBool("enabled", user_enabled);
    prefs.putString("server", user_server);
    prefs.putInt("port", user_port);
    prefs.putString("username", user_username);
    prefs.putString("password", user_password);
    prefs.putString("topic", user_topic);
    prefs.end();
}

static String dev_mqtt_topic() {
    return String("maydokhongkhi/") + TB_HW_ID + "/data";
}

static String dev_ota_topic() {
    return String("maydokhongkhi/") + TB_HW_ID + "/ota";
}

static String dev_cmd_topic() {
    return String("maydokhongkhi/") + TB_HW_ID + "/cmd";
}

// Forward declare — implemented in ota_manager
void ota_startUpdateFromUrl(const char* url);
static void onDevMessage(char* topic, byte* payload, unsigned int length);
static bool connectClient(PubSubClient& client, const char* tag,
                          const char* user = nullptr, const char* pass = nullptr);

// Đã subscribe + MAC exchange trên connection hiện tại chưa
static bool devSubscribed = false;

static unsigned long devSubscribeMs = 0;

// Subscribe topics + ESP-NOW MAC exchange — CHỈ chạy 1 lần cho mỗi connection mới.
// Nếu re-subscribe mỗi chu kỳ, broker trả lại retained MAC liên tục → lastCtrlMacMs
// refresh mãi → espNowCtrlMacStale() không bao giờ fire (ctrl offline không detect được)
static void devDoSubscribe() {
    devClient.setCallback(onDevMessage);
    devClient.subscribe(dev_ota_topic().c_str());
    devClient.subscribe(dev_cmd_topic().c_str());
    Serial.printf("[MQTT-DEV] Subscribed topics: %s | %s\n",
                  dev_ota_topic().c_str(), dev_cmd_topic().c_str());

    // ---- ESP-NOW MAC exchange (ESPNOW_SENSOR_GUIDE.md) ----
    devClient.subscribe(topicCtrlMac.c_str());
    devSubscribeMs = millis();
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char myMac[18];
    snprintf(myMac, sizeof(myMac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    devClient.publish(topicMyMac.c_str(), myMac, true); // retain=true
    Serial.printf("[MQTT-DEV] Published sensor MAC: %s -> %s (sub %s)\n",
                  myMac, topicMyMac.c_str(), topicCtrlMac.c_str());
}

// Duy trì connection MQTT-DEV: reconnect khi rớt, subscribe đúng 1 lần mỗi connection
static void devMaintain() {
    if (!devClient.connected()) {
        devSubscribed = false; // connection cũ mất -> connection mới phải subscribe lại
        connectClient(devClient, "MQTT-DEV");
    }
    if (devClient.connected() && !devSubscribed) {
        devSubscribed = true;
        devDoSubscribe();
    }
}

static void onDevMessage(char* topic, byte* payload, unsigned int length) {
    char buf[256];
    unsigned int n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';

    Serial.printf("[MQTT-DEV] RX %s: %s\n", topic, buf);

    // ---- ESP-NOW: nhận MAC của board điều khiển ----
    if (topicCtrlMac.length() && strcmp(topic, topicCtrlMac.c_str()) == 0) {
        bool isRetained = (millis() - devSubscribeMs < 2000);
        espNowSaveCtrlMac(buf, isRetained);
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
        Serial.println("[MQTT-DEV] JSON parse error");
        return;
    }

    const char* action = doc["action"];
    if (!action) return;

    // ---- OTA ----
    if (strcmp(action, "ota") == 0) {
        const char* url = doc["url"];
        if (url) {
            Serial.printf("[MQTT-DEV] OTA command received: %s\n", url);
            ota_startUpdateFromUrl(url);
        }
        return;
    }

    // ---- ESP-NOW: đổi ctrl_id board điều khiển từ xa ----
    if (strcmp(action, "set_ctrl_id") == 0) {
        const char* id = doc["ctrl_id"];
        if (id && strlen(id) > 0) {
            String oldTopic = topicCtrlMac;
            espNowSetCtrlId(id); // lưu NVS
            topicCtrlMac = String("bodieukhienfuviair/") + espNowGetCtrlId() + "/MACAdd";
            if (devClient.connected() && oldTopic != topicCtrlMac) {
                devClient.unsubscribe(oldTopic.c_str());
                devClient.subscribe(topicCtrlMac.c_str());
            }
            Serial.printf("[MQTT-DEV] ctrl_id -> %s, sub %s\n",
                          espNowGetCtrlId(), topicCtrlMac.c_str());
        }
        return;
    }

    // ---- Calibrate CO2 (SCD30 FRC) ----
    if (strcmp(action, "calibrate_co2") == 0) {
        uint16_t ppm = doc["ppm"] | 400;  // mặc định 400 ppm nếu không gửi
        Serial.printf("[MQTT-DEV] Calibrate CO2 command: %u ppm\n", ppm);
        bool ok = scd30_forceCalibrate(ppm);
        Serial.printf("[MQTT-DEV] Calibrate CO2: %s\n", ok ? "SUCCESS" : "FAILED");
        return;
    }
}

bool mqtt_init() {
    loadUserConfig();

    // Topic ESP-NOW MAC exchange — ctrlId load trong espNowInit() (gọi trước mqtt_init)
    topicMyMac   = String("maydokhongkhi/") + TB_HW_ID + "/MACAdd";
    topicCtrlMac = String("bodieukhienfuviair/") + espNowGetCtrlId() + "/MACAdd";

    devClient.setServer(DEV_MQTT_SERVER, DEV_MQTT_PORT);
    if (strlen(user_server) > 0) userClient.setServer(user_server, user_port);

    Serial.printf("[MQTT-DEV] %s:%d topic=%s\n", DEV_MQTT_SERVER, DEV_MQTT_PORT, dev_mqtt_topic().c_str());
    Serial.printf("[MQTT-USER] %s %s:%d topic=%s\n", user_enabled ? "ON" : "OFF", user_server, user_port, user_topic);
    return true;
}

static bool connectClient(PubSubClient& client, const char* tag,
                          const char* user, const char* pass) {
    if (client.connected()) return true;
    String clientId = String(tag) + "-" + TB_HW_ID + "-" + String(random(0xffff), HEX);
    Serial.printf("[%s] Connecting...", tag);
    bool ok;
    if (user && strlen(user) > 0) ok = client.connect(clientId.c_str(), user, pass ? pass : "");
    else                         ok = client.connect(clientId.c_str());
    Serial.println(ok ? " OK" : " FAIL");
    if (!ok) Serial.printf("[%s] rc=%d\n", tag, client.state());
    return ok;
}

bool mqtt_connect() {
    devMaintain();

    bool userOk = true;
    if (user_enabled && strlen(user_server) > 0 && strlen(user_topic) > 0) {
        userClient.setServer(user_server, user_port);
        userOk = connectClient(userClient, "MQTT-USER", user_username, user_password);
    }
    return devClient.connected() && userOk;
}

bool mqtt_isConnected() {
    bool userCfgOk = !user_enabled || (strlen(user_server) > 0 && strlen(user_topic) > 0);
    return devClient.connected() && (!user_enabled || !userCfgOk || userClient.connected());
}

static void buildPayload(char* payload, size_t payloadSize, float temperature, float humidity, float co2, float lux) {
    JsonDocument doc;
    doc["id_device"] = TB_HW_ID;
    doc["temperature"] = round(temperature * 10) / 10.0;
    doc["humidity"] = round(humidity * 10) / 10.0;
    doc["co2"] = (int)round(co2);
    doc["light"] = (int)round(lux);
    doc["device"] = "ESP32-AirMonitor";

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) doc["timestamp"] = mktime(&timeinfo);
    else                         doc["timestamp"] = millis() / 1000;

    serializeJson(doc, payload, payloadSize);
}

bool mqtt_publishSensorData(float temperature, float humidity, float co2, float lux) {
    mqtt_connect();

    char payload[300];
    buildPayload(payload, sizeof(payload), temperature, humidity, co2, lux);

    bool devOk = false;
    if (devClient.connected()) {
        devOk = devClient.publish(dev_mqtt_topic().c_str(), payload);
        Serial.printf("[MQTT-DEV] %s %s: %s\n", devOk ? "OK" : "FAIL", dev_mqtt_topic().c_str(), payload);
    }

    bool userOk = true;
    if (user_enabled && strlen(user_server) > 0 && strlen(user_topic) > 0) {
        userOk = false;
        if (userClient.connected()) {
            userOk = userClient.publish(user_topic, payload);
            Serial.printf("[MQTT-USER] %s %s: %s\n", userOk ? "OK" : "FAIL", user_topic, payload);
        } else {
            Serial.println("[MQTT-USER] FAIL not connected");
        }
    }
    // MQTT-USER là broker tùy chọn: fail không tính là mất cloud (tránh ESP-NOW fallback + park radio)
    return devOk;
}

void mqtt_loop() {
    devMaintain();
    devClient.loop();

    if (user_enabled && strlen(user_server) > 0 && strlen(user_topic) > 0) {
        if (!userClient.connected()) {
            userClient.setServer(user_server, user_port);
            connectClient(userClient, "MQTT-USER", user_username, user_password);
        }
        userClient.loop();
    }
}

bool mqtt_userEnabled() { return user_enabled; }
void mqtt_setUserEnabled(bool enabled) { user_enabled = enabled; saveUserConfig(); if (!enabled) userClient.disconnect(); }

const char* mqtt_getServer() { return user_server; }
int mqtt_getPort() { return user_port; }
const char* mqtt_getUsername() { return user_username; }
const char* mqtt_getPassword() { return user_password; }
const char* mqtt_getTopic() { return user_topic; }

void mqtt_setUserServer(const char* server) { strncpy(user_server, server, sizeof(user_server) - 1); user_server[sizeof(user_server)-1] = '\0'; saveUserConfig(); }
void mqtt_setUserPort(int port) { user_port = port; saveUserConfig(); }
void mqtt_setUserUsername(const char* username) { strncpy(user_username, username, sizeof(user_username) - 1); user_username[sizeof(user_username)-1] = '\0'; saveUserConfig(); }
void mqtt_setUserPassword(const char* password) { strncpy(user_password, password, sizeof(user_password) - 1); user_password[sizeof(user_password)-1] = '\0'; saveUserConfig(); }
void mqtt_setUserTopic(const char* topic) { strncpy(user_topic, topic, sizeof(user_topic) - 1); user_topic[sizeof(user_topic)-1] = '\0'; saveUserConfig(); }
