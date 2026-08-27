#pragma once

#include <Arduino.h>
#include "records.h"

void initDownlink();
void onRequest();
void downlinkUpdateSuite(bool maxOk, float maxTemp,
                         bool msOk, float msTemp, float msPressure,
                         bool shtOk, float shtTemp, float shtHumidity);
void downlinkUpdateXy1(float t);
void downlinkUpdateXy2(float t);
