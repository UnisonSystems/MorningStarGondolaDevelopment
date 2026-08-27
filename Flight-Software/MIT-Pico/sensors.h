#pragma once

#include <Arduino.h>

extern unsigned long lastBnoUs;
extern unsigned long lastSuiteUs;

bool initSensors();
void prepareMax31865Bus();
void recoverMax31865Bus();
bool readMax31865Safe(uint16_t &raw, float &ratio, float &resistance,
                      float &temperature, uint8_t &fault);
void pollBno(unsigned long nowUs);
void pollSuite(unsigned long nowUs);
