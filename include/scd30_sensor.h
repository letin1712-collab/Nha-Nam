#ifndef SCD30_SENSOR_H
#define SCD30_SENSOR_H

#include <SensirionI2cScd30.h>

bool scd30_init();
bool scd30_readData();
float scd30_getTemperature();
float scd30_getHumidity();
float scd30_getCO2();
bool scd30_isDataReady();

/**
 * Forced Recalibration (FRC): ép SCD30 về giá trị CO2 chuẩn.
 * Gọi khi thiết bị đang ở môi trường biết trước nồng độ CO2
 * (ví dụ: ngoài trời = 400-420 ppm).
 * @param ppm  Giá trị CO2 chuẩn (thường 400 ppm)
 * @return true nếu calib thành công
 */
bool scd30_forceCalibrate(uint16_t ppm);

#endif // SCD30_SENSOR_H
