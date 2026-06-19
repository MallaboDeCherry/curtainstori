#include "MqttManager.h"
#include "Config.h"
#include <Arduino.h>

WiFiClient espClient;
PubSubClient mqttClient(espClient);

const char* mqtt_server = "192.168.2.112";
const int mqtt_port = 1883;

String deviceId = "lecture_blind";

char mqtt_topic_command[64];
char mqtt_topic_status[64];

bool mqttConnected = false;
unsigned long lastMqttReconnectAttempt = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length);

void initMqttTopics() {
    snprintf(mqtt_topic_command, sizeof(mqtt_topic_command), "blinds/%s/set", deviceId.c_str());
    snprintf(mqtt_topic_status, sizeof(mqtt_topic_status), "blinds/%s/position", deviceId.c_str());
    Serial.printf("[MQTT] Топики: %s | %s\n", mqtt_topic_command, mqtt_topic_status);
}

void initMqttClient() {
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    Serial.println("[MQTT] Клиент инициализирован");
}

// ============= ОБНОВЛЕННЫЙ CALLBACK =============
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.printf("[MQTT] 📩 Получено: %s -> %s\n", topic, message.c_str());
    
    if (String(topic) == mqtt_topic_command) {
        int position = message.toInt();
        if (position >= 0 && position <= 100) {
            Serial.printf("🎯 MQTT команда: %d%%\n", position);
            setCurtainPosition(position);
        } else {
            Serial.printf("[MQTT] ⚠️ Неверная позиция: %s\n", message.c_str());
        }
    } else {
        Serial.printf("[MQTT] ⚠️ Неизвестный топик: %s\n", topic);
    }
}

void reconnectMqtt() {
    if (mqttClient.connected()) return;
    
    Serial.print("[MQTT] Подключение...");
    String clientId = "ESP32-Curtain-";
    clientId += String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
        Serial.println(" ✅");
        mqttConnected = true;
        
        // ============= ПОДПИСКА С ПРОВЕРКОЙ =============
        if (mqttClient.subscribe(mqtt_topic_command)) {
            Serial.printf("[MQTT] ✅ Подписан на: %s\n", mqtt_topic_command);
        } else {
            Serial.printf("[MQTT] ❌ Ошибка подписки: %s\n", mqtt_topic_command);
        }
        
        String hello = "{\"device_id\":\"" + deviceId + "\",\"status\":\"online\"}";
        if (mqttClient.publish("devices/announce", hello.c_str())) {
            Serial.printf("[MQTT] Приветствие отправлено: %s\n", hello.c_str());
        }
        
        publishCurtainStatus();
    } else {
        Serial.printf(" ❌ (код: %d)\n", mqttClient.state());
        mqttConnected = false;
    }
}

void mqttLoop() {
    if (!mqttClient.connected()) {
        if (millis() - lastMqttReconnectAttempt > 5000) {
            lastMqttReconnectAttempt = millis();
            reconnectMqtt();
        }
    } else {
        mqttClient.loop();
    }
}

void publishCurtainStatus() {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не подключен, статус не отправлен");
        return;
    }
    
    int position = getCurtainPosition();
    String payload = String(position);
    
    if (mqttClient.publish(mqtt_topic_status, payload.c_str())) {
        Serial.printf("[MQTT] Статус: %s%%\n", payload.c_str());
    } else {
        Serial.println("[MQTT] Ошибка отправки статуса");
    }
}

void setMqttDeviceId(const char* id) {
    deviceId = String(id);
    initMqttTopics();
    Serial.printf("[MQTT] Device ID обновлен: %s\n", deviceId.c_str());
}