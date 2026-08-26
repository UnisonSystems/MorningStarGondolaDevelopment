// cnt_data.h -- shared CNTData snapshot for Tiny4FSK I2C requests

#ifndef CNT_DATA_H
#define CNT_DATA_H

#include <Arduino.h>
#include <stdint.h>
#include <math.h>

#pragma pack(push, 1)
struct CNTData {
  struct {
    uint32_t val1;
    uint32_t val2;
    uint32_t val3;
    uint32_t val4;
  } cnt1;
  struct {
    uint32_t val1;
    uint32_t val2;
    uint32_t val3;
    uint32_t val4;
  } cnt2;
  struct {
    uint32_t val1;
    uint32_t val2;
    uint32_t val3;
    uint32_t val4;
  } cnt3;
  uint8_t temperature;
  uint8_t humidity;
};
#pragma pack(pop)

void cnt_data_store(const CNTData& src);
void cnt_data_load(CNTData& dst);

inline uint32_t current_uA_to_scaled(float uA) {
  if (isnan(uA) || uA < 0.0f) {
    return 0;
  }
  double scaled = (double)uA * 100.0;
  if (scaled > 4294967295.0) {
    return 0xFFFFFFFFu;
  }
  return (uint32_t)(scaled + 0.5);
}

#endif