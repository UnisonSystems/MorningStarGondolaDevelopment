#pragma once

#include <Arduino.h>
#include <Adafruit_MAX31865.h>
#include <ICM_20948.h>
#include "Adafruit_SHT4x.h"
#include "cnt_data.h"

struct SensorSnapshot {
  bool sht45_present;
  bool ms5611_present;
  bool icm2098_present;
  bool max31865_present;
  bool sd_present;
  bool xyt01_present;

  float sht45_temperature_c;
  float sht45_humidity_pct;
  float ms5611_temperature_c;
  float ms5611_pressure_hpa;
  float icm2098_accel_x_g;
  float icm2098_accel_y_g;
  float icm2098_accel_z_g;
  float icm2098_gyro_x_dps;
  float icm2098_gyro_y_dps;
  float icm2098_gyro_z_dps;
  float max31865_temperature_c;
  uint8_t xyt01_status;
  uint8_t sensor_status_bits;
};

void sensors_begin();
void sensors_update(SensorSnapshot& snapshot);
void sensors_apply_to_packet(CNTData& pkt, const SensorSnapshot& snapshot);
