#ifndef ESPNOW_SENDER_H
#define ESPNOW_SENDER_H

#include <Arduino.h>

// Channel fallback khi mất WiFi — PHẢI KHỚP với board điều khiển (ESPNOW_SENSOR_GUIDE.md)
#define ESPNOW_FALLBACK_CH     1
#define ESPNOW_DEFAULT_CTRL_ID "FVACTL001"
// Quá N lần ESP-NOW send FAIL liên tiếp -> ngừng redundancy (giữ WiFi cho cloud)
#define ESPNOW_REDUNDANCY_MAX_FAILS 3

void espNowInit();
void espNowSaveCtrlMac(const char* macStr, bool isRetained = false);
void espNowSendSensorData(float temperature, float humidity, float co2, float lux);
void espNowOnWifiConnected();     // lưu kênh AP hiện tại (gọi khi WiFi đang kết nối)
void espNowOnWifiReconnected();   // reset trạng thái kênh ESP-NOW khi WiFi có lại
void espnow_loop();               // dò kênh router bằng scan khi offline

// Ctrl mất WiFi một mình: sensor online nhưng lâu không nghe MAC ctrl qua MQTT
// (ctrl phải publish MAC định kỳ ~30s như heartbeat) → true = cần gửi ESP-NOW redundancy
bool espNowCtrlMacStale(unsigned long timeoutMs = 60000UL);

// Gate redundancy: false khi ESP-NOW FAIL liên tiếp quá ngưỡng (ctrl unreachable)
bool espNowRedundancyAllowed();

const char* espNowGetCtrlId();
void       espNowSetCtrlId(const char* id);

#endif // ESPNOW_SENDER_H
