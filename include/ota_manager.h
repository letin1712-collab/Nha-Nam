#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

/**
 * Bắt đầu OTA từ URL cụ thể (gọi từ MQTT callback hoặc manual trigger).
 * URL phải trả về firmware binary (HTTP 200).
 */
void ota_startUpdateFromUrl(const char* url);

/**
 * Check version + download firmware.
 * Gọi GET <base_url>?id=<hw_id>&v=<current_version>
 * - HTTP 304: version mới nhất = current → skip, không tải
 * - HTTP 200: có firmware mới → tải về và update
 * - HTTP 404: không tìm thấy firmware cho device này
 */
void ota_checkAndUpdate(const char* baseUrl);

#endif // OTA_MANAGER_H
