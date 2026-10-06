#ifndef THINGSBOARD_CLIENT_H
#define THINGSBOARD_CLIENT_H

#include <Arduino.h>

#define TB_HW_ID        "N4L0E9Q"
#define TB_SECRET_KEY   "GTZMTJ"
#define FIRMWARE_VERSION "1.0.4"

// ---- CoreIoT (app.coreiot.io) ----
#define TB_SERVER       "app.coreiot.io"
#define TB_PORT         1883
// Access token của device trên CoreIoT (Devices -> device -> Copy access token).
// Mỗi máy 1 token riêng. KHÔNG commit token thật lên git.
#define COREIOT_TOKEN   "aW2ZYbE5L0yHyytY59UN"


#define TB_CLAIM_HOST   "gw.fuvitech.vn"
#define TB_CLAIM_PATH   "/api/v1/device-heartbeat/"

#define TB_NVS_NS       "tb-config"
#define TB_NVS_TOKEN    "access_token"
#define TB_NVS_HW_ID    "hw_id"

void tb_init();

/**
 * Thực hiện claim: gọi HTTP GET lên gw.fuvitech.vn để lấy access_token.
 * Lưu token vào NVS nếu thành công.
 * @return true nếu lấy được token hợp lệ
 */
bool tb_claim();

/**
 * Kết nối đến ThingsBoard MQTT broker bằng access_token hiện tại.
 * Nếu chưa có token → tự động claim trước.
 * @return true nếu kết nối thành công
 */
bool tb_connect();

/**
 * Trả về trạng thái kết nối MQTT đến ThingsBoard.
 */
bool tb_isConnected();

/**
 * Publish sensor data lên ThingsBoard (Telemetry + Attribute).
 * Keys:  temp_c | do_mgperl | ph | orp_mv
 * @param temperature °C
 * @param humidity    %
 * @param co2         ppm
 * @param lux         lux
 * @return true nếu publish thành công
 */
bool tb_publishSensorData(float temperature, float humidity, float co2, float lux);

/**
 * Gọi trong loop() để duy trì kết nối MQTT.
 */
void tb_loop();

/**
 * Lấy access token hiện tại (đọc từ RAM).
 */
const char* tb_getAccessToken();

/**
 * Xóa access token khỏi RAM và NVS, ngắt kết nối MQTT.
 * Dùng khi cần re-claim thiết bị (ví dụ: đổi chủ, reset factory).
 * Lần connect tiếp theo sẽ tự động claim lại.
 */
void tb_clearToken();

// Pairing mode: chỉ trong cửa sổ này mới gọi claim để lấy token mới
void tb_startPairingWindow(unsigned long durationMs = 300000UL); // default 5 phút
bool tb_isPairingMode();

#endif // THINGSBOARD_CLIENT_H