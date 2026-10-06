#include "scd30_sensor.h"
#include <Arduino.h>
#include <Wire.h>

#ifndef NO_ERROR
#define NO_ERROR 0
#endif

static SensirionI2cScd30 scd30;

static float scd30_temperature = 0.0f;
static float scd30_humidity = 0.0f;
static float scd30_co2 = 0.0f;

bool scd30_init() {
  Serial.println("Starting SCD30 initialization...");

  scd30.begin(Wire, SCD30_I2C_ADDR_61);

  int16_t error;
  char errorMessage[64];

  error = scd30.stopPeriodicMeasurement();
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }

  error = scd30.softReset();
  delay(2000);

  int8_t serialNumber[32] = {0};
  error = scd30.readSerialNumber(serialNumber, 32);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute readSerialNumber(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  } else {
    Serial.print("SCD30 Serial: ");
    Serial.println((const char*)serialNumber);
  }

  error = scd30.startPeriodicMeasurement(0);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return false;
  }

  Serial.println("SCD30 initialized successfully!");
  return true;
}

bool scd30_readData() {
  float co2 = 0.0f, temp = 0.0f, hum = 0.0f;
  int16_t error = scd30.readMeasurementData(co2, temp, hum);
  if (error != NO_ERROR) {
    Serial.print("SCD30 read error: ");
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.println(msg);
    return false;
  }

  scd30_co2 = co2;
  scd30_temperature = temp;
  scd30_humidity = hum;

  return true;
}

float scd30_getTemperature() { return scd30_temperature; }
float scd30_getHumidity() { return scd30_humidity; }
float scd30_getCO2() { return scd30_co2; }

bool scd30_isDataReady() {
  uint16_t ready = 0;
  int16_t error = scd30.getDataReady(ready);
  return (error == NO_ERROR && ready == 1);
}

bool scd30_forceCalibrate(uint16_t ppm) {
  Serial.printf("[SCD30] Starting FRC calibration at %u ppm...\n", ppm);
  char errorMessage[64];
  int16_t error;

  // Theo datasheet SCD30: phải dừng đo trước khi set FRC
  error = scd30.stopPeriodicMeasurement();
  if (error != NO_ERROR) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("[SCD30] FRC: stopMeasurement error: %s\n", errorMessage);
    return false;
  }
  delay(500);

  // Set Forced Recalibration value
  error = scd30.forceRecalibration(ppm);
  if (error != NO_ERROR) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("[SCD30] FRC: setForcedRecalibration error: %s\n", errorMessage);
    // Restart đo lại dù lỗi
    scd30.startPeriodicMeasurement(0);
    return false;
  }
  delay(500);

  // Restart periodic measurement
  error = scd30.startPeriodicMeasurement(0);
  if (error != NO_ERROR) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("[SCD30] FRC: startMeasurement error: %s\n", errorMessage);
    return false;
  }

  Serial.printf("[SCD30] FRC calibration done! Reference = %u ppm\n", ppm);
  return true;
}
