#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <FS.h>
#include <WiFiManager.h>

// Khởi tạo WiFi với config MQTT
bool wifi_init(const char *ap_name = "ESP32-Sensor",
               const char *ap_password = NULL);

bool wifi_isConnected();

void wifi_reset();

String wifi_getIP();

// Start config portal với MQTT parameters
bool wifi_startConfigPortal(const char *ap_name, const char *ap_password = NULL);

// Lấy MQTT config đã lưu
const char* wifi_getMqttServer();
int wifi_getMqttPort();
const char* wifi_getMqttTopic();
const char* wifi_getDeviceId();  // ⭐ Thêm getter cho Device ID

#endif // WIFI_MANAGER_H
