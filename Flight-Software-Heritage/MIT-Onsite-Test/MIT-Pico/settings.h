// settings.h -- CNT front-end helpers

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

static const uint8_t MCP_ADDR = 0x27;
static const uint8_t MCP_IODIRA = 0x00;
static const uint8_t MCP_GPIOA = 0x12;

static const uint8_t LOCKWR = 0x00;
static const uint8_t LOCKRO = 0x01;
static const uint8_t TIACN_TIAG_2_75_RLOAD_010 = 0x04;
static const uint8_t TIACN_TIAG_7_00_RLOAD_010 = 0x0C;
static const uint8_t TIACN_TIAG_35_0_RLOAD_010 = 0x14;
static const uint8_t TIA_SETTING = TIACN_TIAG_2_75_RLOAD_010;
static const uint8_t REFCN_BIAS_0V5 = 0x9B;
static const uint8_t MODECN_OP_MODE_3LEADAMPC = 0x03;
static const uint8_t MODECN_OP_MODE_DEEPSLEEP = 0x00;

static const float VREF = 2.5f;
static const float VA = 5.0f;
static const float BR = 65535.0f;
static const float SPAN = VA - (VREF / 65536.0f);
static const float INT_ZERO_V = 0.20f * VREF;
static const float R_TIA = 2750.0f;
static const float V_EFF = 0.512f;
static const float I_FLOOR_UA = 0.5f;
static const float V_REPORT = 0.5f;

static const int N_DEVICES = 12;

extern const float R_SERIES[12];

void lmp_write(uint8_t value, uint8_t reg);
void lmp_init(uint8_t LOCK, uint8_t TIACN, uint8_t REFCN, uint8_t MODECN);
int16_t readadc();
float raw_to_current(float raw, float* volts_out = nullptr);
float device_ohms(float loop_uA, int channel);
void mux_init();
void mux_select(uint8_t channel);

#endif