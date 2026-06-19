#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============= ПОДКЛЮЧАЕМ МОДУЛИ =============
#include "MqttManager.h"
#include "CurtainControl.h"  // <-- ДОБАВИТЬ

// ============= ПИНЫ =============
#define LED_PIN 2
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ============= ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ =============
Preferences preferences;
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
BLEAdvertising* pAdvertising = nullptr;

bool bleConnected = false;
bool wifiConnected = false;

// ============= BLE CALLBACK =============
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        bleConnected = true;
        Serial.println("[BLE] ✅ Подключено!");
        digitalWrite(LED_PIN, HIGH);
    }
    void onDisconnect(BLEServer* pServer) {
        bleConnected = false;
        Serial.println("[BLE] ❌ Отключено");
        BLEDevice::getAdvertising()->start();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        Serial.printf("[BLE] Получено: %s\n", value.c_str());
        
        int sep1 = value.indexOf(':');
        int sep2 = value.indexOf(':', sep1 + 1);
        
        String ssid, pass, id;
        
        if (sep1 != -1 && sep2 != -1) {
            ssid = value.substring(0, sep1);
            pass = value.substring(sep1 + 1, sep2);
            id = value.substring(sep2 + 1);
            Serial.printf("[BLE] SSID=%s, ID=%s\n", ssid.c_str(), id.c_str());
        } else if (sep1 != -1) {
            ssid = value.substring(0, sep1);
            pass = value.substring(sep1 + 1);
            id = "blind_" + String(ESP.getEfuseMac(), HEX);
            Serial.printf("[BLE] SSID=%s (ID сгенерирован)\n", ssid.c_str());
        } else {
            Serial.println("[BLE] ❌ Неверный формат");
            pCharacteristic->setValue("ERROR");
            pCharacteristic->notify();
            return;
        }
        
        preferences.begin("wifi-creds", false);
        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        preferences.end();
        
        if (id.length() > 0) {
            setMqttDeviceId(id.c_str());
        }
        
        pCharacteristic->setValue("OK");
        pCharacteristic->notify();
        
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.println("[WiFi] Подключение...");
    }
};

// ============= ГЕНЕРАЦИЯ ID =============
String getDeviceId() {
    preferences.begin("device-config", true);
    String id = preferences.getString("device_id", "");
    preferences.end();
    
    if (id.length() == 0) {
        uint64_t chipId = ESP.getEfuseMac();
        id = "blind_" + String(chipId, HEX);
        preferences.begin("device-config", false);
        preferences.putString("device_id", id);
        preferences.end();
        Serial.printf("[Config] ✅ Создан новый ID: %s\n", id.c_str());
    } else {
        Serial.printf("[Config] 📥 Загружен ID: %s\n", id.c_str());
    }
    return id;
}

// ============= SETUP =============
void setup() {
    Serial.begin(115200);
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║   УМНЫЕ ШТОРЫ (МАКЕТ) v5.3       ║");
    Serial.println("║   BLE ВСЕГДА ВКЛЮЧЕН!              ║");
    Serial.println("║   МОТОР УПРАВЛЯЕТСЯ ЧЕРЕЗ MQTT   ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // ============= ИНИЦИАЛИЗАЦИЯ МОТОРА =============
    initCurtain();
    
    // ============= ЗАГРУЖАЕМ ID =============
    String deviceId = getDeviceId();
    Serial.printf("[Config] Device ID: %s\n", deviceId.c_str());
    
    // ============= УСТАНАВЛИВАЕМ ID В MqttManager =============
    setMqttDeviceId(deviceId.c_str());
    initMqttTopics();
    initMqttClient();
    Serial.println("[MQTT] ✅ Инициализирован");
    
    // ============= ЗАПУСКАЕМ BLE =============
    BLEDevice::init("Умные шторы (макет)");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->setCallbacks(new MyCallbacks());
    pCharacteristic->setValue("OK");
    pService->start();
    
    pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    Serial.println("[BLE] ✅ Запущен");
    Serial.println("[BLE] 📛 Имя: Умные шторы (макет)");
    Serial.printf("[BLE] 🆔 Device ID: %s\n", deviceId.c_str());
    Serial.println("[BLE] 🔵 Ожидание подключения...");
    
    // ============= ПРОВЕРЯЕМ СОХРАНЕННЫЙ WiFi =============
    preferences.begin("wifi-creds", true);
    String savedSsid = preferences.getString("ssid", "");
    String savedPass = preferences.getString("pass", "");
    preferences.end();
    
    if (savedSsid.length() > 0 && savedPass.length() > 0) {
        Serial.printf("[WiFi] 📡 Найден сохраненный WiFi: %s\n", savedSsid.c_str());
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    } else {
        Serial.println("[WiFi] ⚠️ Нет сохраненных настроек");
    }
}

// ============= LOOP =============
void loop() {
    static unsigned long lastBlink = 0;
    static bool ledState = false;
    static unsigned long wifiStartTime = 0;
    static bool wifiConnecting = false;
    
    // ============= ОБНОВЛЕНИЕ МОТОРА (КРИТИЧЕСКИ ВАЖНО!) =============
    updateCurtain();
    
    // ============= MQTT =============
    if (wifiConnected) {
        mqttLoop();
    }
    
    // ============= ОБРАБОТКА WiFi =============
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            wifiConnected = true;
            wifiConnecting = false;
            Serial.println("[WiFi] ✅ Подключен!");
            Serial.print("[WiFi] 📡 IP: ");
            Serial.println(WiFi.localIP());
            Serial.printf("[WiFi] 🆔 Device ID: %s\n", deviceId.c_str());
            reconnectMqtt();
        }
    } else {
        if (wifiConnected) {
            wifiConnected = false;
            Serial.println("[WiFi] ❌ Соединение потеряно");
        }
        
        if (!wifiConnecting) {
            preferences.begin("wifi-creds", true);
            String savedSsid = preferences.getString("ssid", "");
            String savedPass = preferences.getString("pass", "");
            preferences.end();
            
            if (savedSsid.length() > 0 && savedPass.length() > 0) {
                wifiConnecting = true;
                wifiStartTime = millis();
                WiFi.begin(savedSsid.c_str(), savedPass.c_str());
                Serial.println("[WiFi] 🔄 Попытка подключения...");
            }
        }
        
        if (wifiConnecting && (millis() - wifiStartTime > 30000)) {
            wifiConnecting = false;
            Serial.println("[WiFi] ❌ Таймаут подключения");
            preferences.begin("wifi-creds", false);
            preferences.clear();
            preferences.end();
            Serial.println("[WiFi] 🗑️ Настройки сброшены");
            ESP.restart();
        }
    }
    
    // ============= LED ИНДИКАЦИЯ =============
    if (bleConnected) {
        digitalWrite(LED_PIN, HIGH);
    } else {
        if (millis() - lastBlink > 500) {
            lastBlink = millis();
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
    }
    
    delay(10);
}