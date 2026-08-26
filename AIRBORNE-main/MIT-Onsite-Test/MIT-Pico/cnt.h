// cnt.h -- CNT acquisition and snapshot update

#ifndef CNT_H
#define CNT_H

#include <Arduino.h>

void cnt_begin();
void cnt_loop();
bool cnt_scan_once();

#endif