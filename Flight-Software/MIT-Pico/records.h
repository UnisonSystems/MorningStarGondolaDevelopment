#pragma once

#include <Arduino.h>
#include <stdint.h>

enum RecordType : uint8_t {
    REC_BNO086  = 1,
    REC_SUITE   = 2,
    REC_XYT01_1 = 3,
    REC_XYT01_2 = 4
};

#pragma pack(push, 1)

struct BnoRecord {
    uint8_t  type;          // REC_BNO086
    uint32_t timestamp_us;
    uint32_t sequence;
    float ax, ay, az;
    float gx, gy, gz;
    float mx, my, mz;
    float qw, qx, qy, qz;
};

struct SuiteRecord {
    uint8_t  type;          // REC_SUITE
    uint32_t timestamp_us;
    uint32_t sequence;
    float    max_temp;
    float    max_resistance;
    uint16_t max_raw;
    float    max_ratio;
    uint8_t  max_fault;
    float    ms_pressure;
    float    ms_temp;
    float    sht_temp;
    float    sht_humidity;
    uint8_t  sht_crc_ok;
};

struct XyRecord {
    uint8_t  type;          // REC_XYT01_1 or REC_XYT01_2
    uint32_t timestamp_us;
    uint32_t sequence;
    float    temperature;
};

struct DownlinkSnapshot {
    int16_t  t_max31865; // °C × 100
    int16_t  t_sht45;    // °C × 100
    int16_t  t_ms5611;   // °C × 100
    int16_t  t_xyt011;   // °C × 100   XY-T01 #1
    int16_t  t_xyt012;   // °C × 100   XY-T01 #2
    uint16_t h_sht45;    // %RH × 100
    float    p_ms5611;   // mbar, IEEE-754
};

#pragma pack(pop)

static_assert(sizeof(DownlinkSnapshot) == 16, "downlink blob must be 16 bytes");

// Downlink sentinels: never-valid or latest sample failed
static const int16_t  DL_TEMP_SENTINEL = INT16_MIN;
static const uint16_t DL_HUM_SENTINEL  = UINT16_MAX;
// p_ms5611 sentinel is NAN
