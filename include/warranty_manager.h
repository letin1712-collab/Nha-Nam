#pragma once
#include <Arduino.h>

// Bảo hành 1 năm = 365 ngày kể từ lần đầu tiên publish MQTT dev thành công
#define WARRANTY_DAYS_TOTAL 365

/**
 * Kiểm tra NVS, nếu chưa có startTime thì chưa kích hoạt.
 * Gọi khi boot để load trạng thái bảo hành.
 */
void warranty_init();

/**
 * Nếu chưa kích hoạt bảo hành, ghi timestamp hiện tại vào NVS.
 * Gọi sau lần đầu tiên publish MQTT dev thành công.
 */
void warranty_startIfNeeded();

/**
 * Trả về số ngày bảo hành còn lại (0 nếu hết hạn, -1 nếu chưa kích hoạt).
 */
int warranty_daysLeft();

/**
 * Reset bảo hành về chưa kích hoạt (xóa NVS).
 * Dùng cho combo: hold BOOT 10s + tap BOOT 3 lần.
 */
void warranty_reset();
