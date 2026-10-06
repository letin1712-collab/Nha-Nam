#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <PubSubClient.h>

#include "thingsboard_client.h" // để lấy TB_HW_ID

#define DEV_MQTT_SERVER "mqtt.fuvitech.vn"
#define DEV_MQTT_PORT   1883
// Topic sẽ được generate động dựa vào TB_HW_ID
//#define DEV_MQTT_TOPIC  "maydokhongkhi/tb001"

bool mqtt_init();
bool mqtt_connect();
bool mqtt_isConnected();
bool mqtt_publishSensorData(float temperature, float humidity, float co2, float lux);
void mqtt_loop();

bool mqtt_userEnabled();
void mqtt_setUserEnabled(bool enabled);

const char* mqtt_getServer();
int mqtt_getPort();
const char* mqtt_getUsername();
const char* mqtt_getPassword();
const char* mqtt_getTopic();

void mqtt_setUserServer(const char* server);
void mqtt_setUserPort(int port);
void mqtt_setUserUsername(const char* username);
void mqtt_setUserPassword(const char* password);
void mqtt_setUserTopic(const char* topic);

#endif // MQTT_CLIENT_H
