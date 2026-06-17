#include <Arduino.h>
#include "Config.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "CurtainControl.h"

// ============= СОСТОЯНИЯ =============
enum DeviceState { SETUP_BLE, CONNECTING_WIFI, WORKING_WS };
DeviceState currentState = SETUP_BLE;

// ============= ГЛОБАЛЬНЫЕ ОБЪЕКТЫ =============
Preferences preferences;
WebServer server(80);

unsigned long lastLedBlink = 0;
bool ledState = false;

// Индикация подключения приложения
bool isAppConnected = false;
unsigned long lastAppActivity = 0;
const unsigned long APP_TIMEOUT = 10000;

void updateAppActivity() {
    lastAppActivity = millis();
    if (!isAppConnected) {
        isAppConnected = true;
        Serial.println("📱 Приложение подключено!");
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, LOW);
            delay(100);
        }
    }
}

void checkAppConnection() {
    if (isAppConnected && (millis() - lastAppActivity > APP_TIMEOUT)) {
        isAppConnected = false;
        Serial.println("📱 Приложение отключено (таймаут)");
    }
}

// ============= BLE ОБРАБОТЧИК =============
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        if (value.length() > 0) {
            int sep = value.indexOf(':');
            if (sep != -1) {
                String ssid = value.substring(0, sep);
                String pass = value.substring(sep + 1);
                
                Serial.println("📡 Сохраняю настройки WiFi...");
                preferences.begin("wifi-creds", false);
                preferences.putString("ssid", ssid);
                preferences.putString("pass", pass);
                preferences.end();

                Serial.printf("✅ Получен SSID: %s\n", ssid.c_str());
                WiFi.begin(ssid.c_str(), pass.c_str());
                currentState = CONNECTING_WIFI;
            }
        }
    }
};

// ============= ФУНКЦИИ ДЛЯ ANDROID API =============

void sendCurtainStatus() {
    CurtainState state = getCurtainStatus();
    int position = getCurtainPosition();
    
    String stateStr;
    switch(state) {
        case OPENED: stateStr = "OPENED"; break;
        case CLOSED: stateStr = "CLOSED"; break;
        case MOVING: stateStr = "MOVING"; break;
        default: stateStr = "STOPPED"; break;
    }
    
    JsonDocument doc;
    doc["status"] = stateStr;
    doc["position"] = position;
    doc["is_moving"] = (state == MOVING);
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void sendErrorResponse(const String& message) {
    JsonDocument doc;
    doc["error"] = message;
    String response;
    serializeJson(doc, response);
    server.send(400, "application/json", response);
}

// ============= API ОБРАБОТЧИКИ =============

void handlePing() {
    Serial.println("-> GET /ping");
    updateAppActivity();
    server.send(200, "text/plain", "pong");
}

void handleOpen() {
    Serial.println("-> GET /open");
    updateAppActivity();
    openCurtain();
    sendCurtainStatus();
}

void handleClose() {
    Serial.println("-> GET /close");
    updateAppActivity();
    closeCurtain();
    sendCurtainStatus();
}

void handleStop() {
    Serial.println("-> GET /stop");
    updateAppActivity();
    stopCurtain();
    sendCurtainStatus();
}

void handleStatus() {
    Serial.println("-> GET /status");
    updateAppActivity();
    sendCurtainStatus();
}

void handleSetPosition() {
    Serial.println("-> GET /set");
    updateAppActivity();
    if (server.hasArg("pos")) {
        int pos = server.arg("pos").toInt();
        if (pos < 0 || pos > 100) {
            sendErrorResponse("Position must be 0-100");
            return;
        }
        
        Serial.printf("🎯 Установка позиции: %d%%\n", pos);
        int currentPos = getCurtainPosition();
        
        if (pos > currentPos) {
            openCurtain();
        } else if (pos < currentPos) {
            closeCurtain();
        }
        
        sendCurtainStatus();
    } else {
        sendErrorResponse("Missing position parameter");
    }
}

void handleInfo() {
    Serial.println("-> GET /info");
    updateAppActivity();
    JsonDocument doc;
    doc["device"] = "Smart Curtain";
    doc["version"] = "2.0";
    doc["ip"] = WiFi.localIP().toString();
    doc["position"] = getCurtainPosition();
    doc["app_connected"] = isAppConnected;
    doc["free_heap"] = ESP.getFreeHeap();
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleCalibrate() {
    Serial.println("-> GET /calibrate");
    updateAppActivity();
    calibrateCurtain();
    sendCurtainStatus();
}

void handleReset() {
    Serial.println("-> GET /reset");
    updateAppActivity();
    resetCurtainSettings();
    server.send(200, "application/json", "{\"status\":\"resetting\"}");
    delay(1000);
    ESP.restart();
}

void handleNotFound() {
    Serial.printf("❌ 404: %s\n", server.uri().c_str());
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
}

// ============= LED ИНДИКАЦИЯ =============
void updateLED() {
    if (currentState == SETUP_BLE) {
        if (millis() - lastLedBlink > 500) {
            lastLedBlink = millis();
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
    } else if (currentState == CONNECTING_WIFI) {
        if (millis() - lastLedBlink > 1000) {
            lastLedBlink = millis();
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
    } else if (currentState == WORKING_WS) {
        if (isAppConnected) {
            digitalWrite(LED_PIN, HIGH);
        } else {
            if (millis() - lastLedBlink > 3000) {
                lastLedBlink = millis();
                digitalWrite(LED_PIN, HIGH);
                delay(100);
                digitalWrite(LED_PIN, LOW);
            }
        }
    }
}

// ============= SETUP =============
void setup() {
    Serial.begin(115200);
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║     SMART CURTAIN ESP32 v2.0       ║");
    Serial.println("╚════════════════════════════════════╝\n");
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    initCurtain();

    preferences.begin("wifi-creds", true);
    String savedSsid = preferences.getString("ssid", "");
    String savedPass = preferences.getString("pass", "");
    preferences.end();

    if (savedSsid.length() > 0 && savedPass.length() > 0) {
        Serial.printf("📡 Найдены сохраненные настройки: %s\n", savedSsid.c_str());
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());
        currentState = CONNECTING_WIFI;
    } else {
        Serial.println("⚠️ Нет сохраненных настроек. Запуск BLE...");
        BLEDevice::init("SmartCurtain");
        BLEServer *pServer = BLEDevice::createServer();
        BLEService *pService = pServer->createService(SERVICE_UUID);
        
        BLECharacteristic *pChar = pService->createCharacteristic(
            CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_WRITE
        );
        
        pChar->setCallbacks(new MyCallbacks());
        pService->start();
        BLEDevice::getAdvertising()->start();
        
        Serial.println("🔵 BLE запущен. Жду конфигурацию...");
        currentState = SETUP_BLE;
    }
}

// ============= LOOP =============
void loop() {
    updateLED();
    
    if (currentState == SETUP_BLE) {
        delay(100);
        return;
    }

    if (currentState == CONNECTING_WIFI) {
        static unsigned long lastPrint = 0;
        static unsigned long wifiStartTime = millis();
        
        if (millis() - lastPrint > 1000) {
            lastPrint = millis();
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✅ WiFi подключен!");
            Serial.print("📡 IP: ");
            Serial.println(WiFi.localIP());

            if (MDNS.begin("smart-curtain")) {
                Serial.println("✅ mDNS: smart-curtain.local");
                MDNS.addService("http", "tcp", 80);
            }

            server.on("/ping", handlePing);
            server.on("/open", handleOpen);
            server.on("/close", handleClose);
            server.on("/stop", handleStop);
            server.on("/status", handleStatus);
            server.on("/set", handleSetPosition);
            server.on("/info", handleInfo);
            server.on("/calibrate", handleCalibrate);
            server.on("/reset", handleReset);
            server.onNotFound(handleNotFound);
            
            server.begin();
            Serial.println("🌐 HTTP сервер запущен!");
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            Serial.println("Доступные API:");
            Serial.println("  GET /ping      - проверка связи");
            Serial.println("  GET /open      - открыть штору");
            Serial.println("  GET /close     - закрыть штору");
            Serial.println("  GET /stop      - остановить");
            Serial.println("  GET /status    - получить статус");
            Serial.println("  GET /set?pos=N - установить позицию N%");
            Serial.println("  GET /info      - информация об устройстве");
            Serial.println("  GET /calibrate - калибровка штор");
            Serial.println("  GET /reset     - сброс настроек");
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

            currentState = WORKING_WS;
        }
        
        if (millis() - wifiStartTime > 30000 && WiFi.status() != WL_CONNECTED) {
            Serial.println("\n❌ Таймаут подключения к Wi-Fi");
            preferences.begin("wifi-creds", false);
            preferences.clear();
            preferences.end();
            ESP.restart();
        }
    }

    if (currentState == WORKING_WS) {
        server.handleClient();
        updateCurtain();
        checkAppConnection();
    }
    
    delay(5);
}