// settings.cpp -- CNT front-end helpers

#include "settings.h"
#include "pinout.h"
#include <SPI.h>
#include <Wire.h>

const float R_SERIES[12] = {
    435.0f, 447.0f, 441.0f, 446.0f, 431.0f, 437.0f,
    439.0f, 433.0f, 435.0f, 431.0f, 443.0f, 432.0f
};

static SPISettings spi_settings_adc(1000000, MSBFIRST, SPI_MODE1);

void lmp_write(uint8_t value, uint8_t reg) {
  Wire.beginTransmission(LMP91000_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void lmp_init(uint8_t LOCK, uint8_t TIACN, uint8_t REFCN, uint8_t MODECN) {
  lmp_write(LOCK, 1);
  lmp_write(TIACN, 16);
  lmp_write(REFCN, 17);
  lmp_write(MODECN, 18);
}

int16_t readadc() {
  uint8_t r[3] = {0, 0, 0};

  SPI.beginTransaction(spi_settings_adc);
  digitalWrite(SPI0_CS_CNT_PIN, LOW);
  r[0] = SPI.transfer(0x00);
  r[1] = SPI.transfer(0x00);
  r[2] = SPI.transfer(0x00);
  digitalWrite(SPI0_CS_CNT_PIN, HIGH);
  SPI.endTransaction();

  uint32_t raw24 = ((uint32_t)r[0] << 16) | ((uint32_t)r[1] << 8) | (uint32_t)r[2];
  uint16_t u16 = (uint16_t)((raw24 >> 6) & 0xFFFF);
  return (int16_t)u16;
}

float raw_to_current(float raw, float* volts_out) {
  float volts = (raw * SPAN) / BR + VREF;
  float current_uA = ((volts - INT_ZERO_V) / R_TIA) * 1000000.0f;
  if (volts_out) {
    *volts_out = volts;
  }
  return current_uA;
}

float device_ohms(float loop_uA, int channel) {
  if (isnan(loop_uA) || loop_uA < I_FLOOR_UA) {
    return NAN;
  }
  if (channel < 0 || channel >= N_DEVICES) {
    return NAN;
  }
  return V_EFF / (loop_uA * 1e-6f) - R_SERIES[channel];
}

void mux_init() {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(MCP_IODIRA);
  Wire.write(0x00);
  Wire.endTransmission();
}

void mux_select(uint8_t channel) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(MCP_GPIOA);
  Wire.write(channel & 0x0F);
  Wire.endTransmission();
}