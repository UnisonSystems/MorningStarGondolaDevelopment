#include "tiny4fsk.h"
#include "cnt_data.h"
#include <Wire.h>

void requestEvent() {
    CNTData dataPacket;
    cnt_data_load(dataPacket);
    Wire1.write((uint8_t*)&dataPacket, sizeof(dataPacket));
}