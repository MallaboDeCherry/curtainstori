#include <Arduino.h>
#include "CurtainControl.h"
#include "Config.h"
#include <Preferences.h>
#include "MqttManager.h"

// ============= ПИНЫ =============
#define DIR_PIN 18
#define STEP_PIN 17
#define ENABLE_PIN 19
#define STEP_DELAY 200
#define MOVE_TIME 30000

// ============= ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ =============
Preferences prefs;
CurtainState currentStatus = CLOSED;
bool isMoving = false;
bool direction = true;
int currentStep = 0;
int totalSteps = 10000;
int currentPosition = 0;
int targetPosition = -1;
unsigned long moveStartTime = 0;
unsigned long lastStepTime = 0;
unsigned long lastPublishTime = 0;  // ← ДЛЯ ОГРАНИЧЕНИЯ ПУБЛИКАЦИЙ
const unsigned long PUBLISH_INTERVAL = 500;  // ← ПУБЛИКУЕМ КАЖДЫЕ 500 мс

// ============= ФУНКЦИИ =============

void savePosition() {
    prefs.begin("curtain", false);
    prefs.putInt("position", currentPosition);
    prefs.putInt("step", currentStep);
    prefs.putInt("totalSteps", totalSteps);
    prefs.end();
    Serial.println("[Prefs] Позиция сохранена");
}

void loadPosition() {
    prefs.begin("curtain", true);
    currentPosition = prefs.getInt("position", 0);
    currentStep = prefs.getInt("step", 0);
    totalSteps = prefs.getInt("totalSteps", 10000);
    prefs.end();
    if (currentPosition >= 99) {
        currentStatus = OPENED;
    } else if (currentPosition <= 1) {
        currentStatus = CLOSED;
    } else {
        currentStatus = STOPPED;
    }
    Serial.printf("[Prefs] Загружена позиция: %d%%, шаг: %d, всего шагов: %d\n", 
                  currentPosition, currentStep, totalSteps);
}

void updatePosition() {
    if (totalSteps > 0) {
        currentPosition = (currentStep * 100) / totalSteps;
        currentPosition = constrain(currentPosition, 0, 100);
    }
}

void initCurtain() {
    pinMode(DIR_PIN, OUTPUT);
    pinMode(STEP_PIN, OUTPUT);
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, HIGH);
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN, LOW);
    loadPosition();
    Serial.printf("[Module] Логика шторы инициализирована, позиция: %d%%\n", currentPosition);
}

void startMotor(bool dir) {
    direction = dir;
    digitalWrite(DIR_PIN, direction);
    digitalWrite(ENABLE_PIN, LOW);
    digitalWrite(STEP_PIN, LOW);
    isMoving = true;
    moveStartTime = millis();
    lastStepTime = micros();
    lastPublishTime = millis();  // ← СБРАСЫВАЕМ ТАЙМЕР
    currentStatus = MOVING;
    Serial.printf("[Motor] %s, текущая позиция: %d%%, цель: %d%%\n", 
                  direction ? "ОТКРЫТИЕ" : "ЗАКРЫТИЕ", currentPosition, targetPosition);
}

void openCurtain() {
    if (currentStatus != OPENED && currentStatus != MOVING) {
        Serial.println("[Motor] Открытие...");
        startMotor(true);
    }
}

void closeCurtain() {
    if (currentStatus != CLOSED && currentStatus != MOVING) {
        Serial.println("[Motor] Закрытие...");
        startMotor(false);
    }
}

void stopMotor() {
    if (!isMoving) return;
    digitalWrite(ENABLE_PIN, HIGH);
    isMoving = false;
    if (currentPosition >= 99) {
        currentStatus = OPENED;
    } else if (currentPosition <= 1) {
        currentStatus = CLOSED;
    } else {
        currentStatus = STOPPED;
    }
    savePosition();
    Serial.printf("[Motor] СТОП на позиции: %d%%\n", currentPosition);
    publishCurtainStatus();  // ← ФИНАЛЬНАЯ ПУБЛИКАЦИЯ
}

void setTargetPosition(int percent) {
    targetPosition = constrain(percent, 0, 100);
    Serial.printf("[Motor] Установлена целевая позиция: %d%%\n", targetPosition);
}

int getTargetPosition() {
    return targetPosition;
}

void setCurtainPosition(int percent) {
    percent = constrain(percent, 0, 100);
    int currentPos = getCurtainPosition();
    if (percent == currentPos) {
        Serial.printf("[Motor] Уже на позиции: %d%%\n", percent);
        return;
    }
    setTargetPosition(percent);
    if (percent > currentPos) {
        openCurtain();
    } else if (percent < currentPos) {
        closeCurtain();
    }
}

int getCurtainPosition() {
    return currentPosition;
}

// ============= ОБНОВЛЕННАЯ ФУНКЦИЯ updateCurtain =============
void updateCurtain() {
    if (!isMoving) return;

    // Таймаут
    if (millis() - moveStartTime >= MOVE_TIME) {
        stopMotor();
        if (direction) {
            currentPosition = 100;
            currentStatus = OPENED;
        } else {
            currentPosition = 0;
            currentStatus = CLOSED;
        }
        updatePosition();
        savePosition();
        targetPosition = -1;
        Serial.println("[Motor] Движение завершено по таймеру");
        publishCurtainStatus();
        return;
    }

    // Генерация шага
    if (micros() - lastStepTime >= STEP_DELAY) {
        lastStepTime = micros();
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(STEP_PIN, LOW);
        
        if (direction) {
            currentStep++;
        } else {
            currentStep--;
        }
        updatePosition();

        // ============= ПУБЛИКАЦИЯ СТАТУСА В РЕАЛЬНОМ ВРЕМЕНИ =============
        // Публикуем каждые PUBLISH_INTERVAL мс (500 мс)
        if (millis() - lastPublishTime >= PUBLISH_INTERVAL) {
            lastPublishTime = millis();
            publishCurtainStatus();  // ← ОТПРАВЛЯЕМ ТЕКУЩУЮ ПОЗИЦИЮ
        }

        // Проверка достижения цели
        if (targetPosition >= 0) {
            if ((direction && currentPosition >= targetPosition) || 
                (!direction && currentPosition <= targetPosition)) {
                stopMotor();
                currentPosition = targetPosition;
                updatePosition();
                savePosition();
                targetPosition = -1;
                Serial.printf("[Motor] Достигнута цель: %d%%\n", currentPosition);
                publishCurtainStatus();  // ← ФИНАЛЬНАЯ ПУБЛИКАЦИЯ
            }
        }

        // Отладка (каждые 500 шагов)
        static int lastDebugStep = 0;
        if (abs(currentStep - lastDebugStep) > 500) {
            lastDebugStep = currentStep;
            Serial.printf("[Motor] Шаг: %d, позиция: %d%%\n", currentStep, currentPosition);
        }
    }
}

CurtainState getCurtainStatus() {
    return currentStatus;
}

void calibrateCurtain() {
    Serial.println("[Calib] НАЧАЛО КАЛИБРОВКИ");
    Serial.println("[Calib] Закрытие до упора...");
    targetPosition = 0;
    startMotor(false);
    unsigned long startTime = millis();
    while (isMoving && (millis() - startTime < MOVE_TIME)) {
        updateCurtain();
        delay(5);
    }
    stopMotor();
    currentStep = 0;
    currentPosition = 0;
    updatePosition();
    delay(1000);
    
    Serial.println("[Calib] Открытие до упора...");
    targetPosition = 100;
    startMotor(true);
    startTime = millis();
    while (isMoving && (millis() - startTime < MOVE_TIME)) {
        updateCurtain();
        delay(5);
    }
    stopMotor();
    totalSteps = currentStep;
    currentPosition = 100;
    updatePosition();
    targetPosition = -1;
    savePosition();
    Serial.printf("[Calib] КАЛИБРОВКА ЗАВЕРШЕНА!\n");
    Serial.printf("[Calib] Всего шагов: %d\n", totalSteps);
    Serial.printf("[Calib] Позиция: %d%%\n", currentPosition);
    publishCurtainStatus();
}

void resetCurtainSettings() {
    Serial.println("[Reset] СБРОС ВСЕХ НАСТРОЕК");
    prefs.begin("curtain", false);
    prefs.clear();
    prefs.end();
    prefs.begin("wifi-creds", false);
    prefs.clear();
    prefs.end();
    currentStep = 0;
    currentPosition = 0;
    totalSteps = 10000;
    currentStatus = CLOSED;
    targetPosition = -1;
    isMoving = false;
    Serial.println("[Reset] Настройки сброшены! Перезагрузка...");
    delay(1000);
    ESP.restart();
}