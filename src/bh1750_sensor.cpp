#include "bh1750_sensor.h"
#include <Arduino.h>
#include <BH1750.h>


static BH1750 lightMeter;

bool bh1750_init() {
  Serial.println("Starting BH1750 initialization...");

  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("Failed to find BH1750 chip");
    return false;
  }

  Serial.println("BH1750 Found!");
  return true;
}

float bh1750_readLight() {
  float lux = lightMeter.readLightLevel();

  // Serial.print("Light: ");
  // Serial.print(lux);
  // Serial.println(" lx");

  return lux;
}
