#ifndef CURTAIN_CONTROL_H
#define CURTAIN_CONTROL_H

#include <Arduino.h>

// Состояния шторы
enum CurtainState { CLOSED, OPENED, MOVING, STOPPED };

// ============= БАЗОВЫЕ ФУНКЦИИ =============
void initCurtain();                 // Инициализация пинов
void openCurtain();                 // Команда на открытие
void closeCurtain();                // Команда на закрытие
void stopCurtain();                 // Команда на остановку
CurtainState getCurtainStatus();    // Геттер состояния

// ============= ФУНКЦИИ ДЛЯ ПОЗИЦИОНИРОВАНИЯ =============
int getCurtainPosition();           // Возвращает позицию в процентах (0-100)
void setCurtainPosition(int percent); // Установить позицию (0-100%)
int getTargetPosition();            // Возвращает целевую позицию
void setTargetPosition(int percent); // Установить целевую позицию

// ============= ФУНКЦИИ ДЛЯ ANDROID =============
void updateCurtain();               // Обновление мотора (вызывать в loop)

// ============= ДОПОЛНИТЕЛЬНЫЕ ФУНКЦИИ =============
void calibrateCurtain();            // Калибровка крайних положений
void resetCurtainSettings();        // Сброс всех настроек
void savePosition();                // Сохранение позиции в Flash
void loadPosition();                // Загрузка позиции из Flash

#endif