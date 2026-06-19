#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <PubSubClient.h>
#include <WiFiClient.h>
#include "CurtainControl.h"

extern WiFiClient espClient;
extern PubSubClient mqttClient;

extern char mqtt_topic_command[64];
extern char mqtt_topic_status[64];
extern unsigned long lastMqttReconnectAttempt;

// ============= ДОБАВИТЬ ЭТУ СТРОКУ =============
extern String deviceId;

void initMqttTopics();
void initMqttClient();
void mqttLoop();
void reconnectMqtt();
void publishCurtainStatus();
void setMqttDeviceId(const char* id);

#endif