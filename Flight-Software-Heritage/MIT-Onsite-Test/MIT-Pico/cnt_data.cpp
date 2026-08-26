// cnt_data.cpp -- shared CNTData snapshot storage

#include "cnt_data.h"
#include <string.h>

static CNTData g_cnt_data;
static volatile bool g_cnt_data_valid = false;

void cnt_data_store(const CNTData& src) {
  noInterrupts();
  memcpy(&g_cnt_data, &src, sizeof(CNTData));
  g_cnt_data_valid = true;
  interrupts();
}

void cnt_data_load(CNTData& dst) {
  noInterrupts();
  if (g_cnt_data_valid) {
    memcpy(&dst, &g_cnt_data, sizeof(CNTData));
  } else {
    memset(&dst, 0, sizeof(CNTData));
  }
  interrupts();
}