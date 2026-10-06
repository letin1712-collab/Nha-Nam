#ifndef SHT30_SENSOR_H
#define SHT30_SENSOR_H

bool sht30_init();
bool sht30_readData();
float sht30_getTemperature();
float sht30_getHumidity();

#endif // SHT30_SENSOR_H
