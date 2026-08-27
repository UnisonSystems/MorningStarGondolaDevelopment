#pragma once

#include <Arduino.h>

extern uint32_t globalSequence;
extern unsigned long lastFlushMs;

bool initLog();
void endSdTransaction();
void requestFlush();
void writeRecord(const void *data, size_t len);
void serviceFlush();
