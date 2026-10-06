#include "sht30_sensor.h"
#include <Arduino.h>
#include <Wire.h>
#include <ArtronShop_SHT3x.h>

static ArtronShop_SHT3x sht3x(0x44, &Wire);
static float sht30_temperature = 0.0f;
static float sht30_humidity = 0.0f;

bool sht30_init() {
    Serial.println("Starting SHT30 initialization...");

    if (!sht3x.begin()) {
        Serial.println("Warning: SHT30 not found!");
        return false;
    }

    Serial.println("✓ SHT30 initialized successfully!");
    return true;
}

bool sht30_readData() {
    if (!sht3x.measure()) {
        Serial.println("SHT30 read error");
        return false;
    }

    sht30_temperature = sht3x.temperature();
    sht30_humidity = sht3x.humidity();
    return true;
}

float sht30_getTemperature() {
    return sht30_temperature;
}

float sht30_getHumidity() {
    return sht30_humidity;
}
