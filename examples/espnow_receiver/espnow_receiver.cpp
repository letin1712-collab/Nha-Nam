// ESP-NOW test receiver — dùng để verify board cảm biến gửi ESP-NOW được hay không
// (không cần board điều khiển thật)
//
// Cách dùng:
// 1. Nạp sketch này vào 1 ESP32 khác
// 2. Mở serial monitor 115200, xem log "MAC:" — báo MAC này cho ai đó so sánh nếu cần
// 3. Board cảm biến gửi unicast tới MAC board ctrl, KHÔNG gửi tới ESP32 này
//    -> tạm thời đổi MAC trong NVS board cảm biến thành MAC ESP32 này, hoặc đợi
//       board cảm biến luân phiên kênh rồi quan sát.
//
// LƯU Ý: sketch này đứng im trên KÊNH 1 (ESPNOW_FALLBACK_CH). Nếu muốn nghe kênh
// khác, sửa ESPNOW_TEST_CH bên dưới.

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

#define ESPNOW_TEST_CH 1

static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
    char buf[251];
    int n = len < 250 ? len : 250;
    memcpy(buf, data, n);
    buf[n] = '\0';
    Serial.printf("[ESPNOW] RX from %02X:%02X:%02X:%02X:%02X:%02X: %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], buf);
}

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    esp_wifi_stop();
    esp_wifi_start();
    // Ép đứng im trên kênh test (không scan, không connect)
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_TEST_CH, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    esp_now_init();
    esp_now_register_recv_cb(onRecv);

    Serial.printf("[ESPNOW] test receiver on channel %d, MAC: ", ESPNOW_TEST_CH);
    Serial.println(WiFi.macAddress());
}

void loop() {}
