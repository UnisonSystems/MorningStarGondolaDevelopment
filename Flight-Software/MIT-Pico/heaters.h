#pragma once

#include <Arduino.h>

void errorBlink();
void sendCommand(HardwareSerial &port, const char *cmd);
float extractTemperature(const String &line);
void initXy();
void performStartupSequence();
void pollXy(unsigned long nowUs);
