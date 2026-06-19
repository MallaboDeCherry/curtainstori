#ifndef CURTAIN_CONTROL_H
#define CURTAIN_CONTROL_H

#include <Arduino.h>
#include <Preferences.h>

// ============= СОСТОЯНИЯ =============
enum CurtainState { CLOSED, OPENED, MOVING, STOPPED };

// ============= ВНЕШНИЕ ПЕРЕМЕННЫЕ =============
extern Preferences prefs;
extern CurtainState currentStatus;
extern bool isMoving;
extern bool direction;
extern int currentStep;
extern int totalSteps;
extern int currentPosition;
extern int targetPosition;
extern unsigned long moveStartTime;
extern unsigned long lastStepTime;

// ============= ФУНКЦИИ =============
void initCurtain();
void savePosition();
void loadPosition();
void updatePosition();
void startMotor(bool dir);
void openCurtain();
void closeCurtain();
void stopMotor();
void setCurtainPosition(int percent);
void setTargetPosition(int percent);
int getTargetPosition();
int getCurtainPosition();
void updateCurtain();
CurtainState getCurtainStatus();
void calibrateCurtain();
void resetCurtainSettings();

#endif