#include <Arduino.h>
#pragma once
// External status LED
static const uint8_t STATUS_LED_PIN = 14;  // GP14

// CNT I2C0: LMP91000 (0x48) + MCP23017 (0x27)
static const uint8_t CNT_SDA_PIN = 4;      // GP4 I2C0 SDA
static const uint8_t CNT_SCL_PIN = 5;      // GP5 I2C0 SCL
static const uint32_t CNT_I2C_FREQ = 400000;

// I2C0: CNT + additional sensors (SHT45, MS5611, ICM-20948)
// I2C1: Tiny4FSK only (Pico = slave 0x09)
static const uint8_t I2C1_SDA_PIN = 2;     // GP2
static const uint8_t I2C1_SCL_PIN = 3;     // GP3
static const uint32_t I2C1_FREQ = 400000;
static const uint8_t TINY4FSK_SLAVE_ADDR = 0x09;

// Shared SPI0: CNT ADC, MAX31865, Micro SD
static const uint8_t SPI0_MISO_PIN = 16;   // GP16
static const uint8_t SPI0_CS_CNT_PIN = 17; // GP17 CNT CS
static const uint8_t SPI0_SCK_PIN = 18;    // GP18
static const uint8_t SPI0_MOSI_PIN = 19;   // GP19
static const uint8_t MAX31865_CS_PIN = 20; // GP20
static const uint8_t SD_CS_PIN = 21;       // GP21

// XY-T01 heaters (9600 8N1, no flow control)
// Pico TX -> heater RX; Pico RX -> heater TX
static const uint8_t XYT01_1_TX_PIN = 0;   // GP0 UART0 TX -> #1 RX
static const uint8_t XYT01_1_RX_PIN = 1;   // GP1 UART0 RX <- #1 TX
static const uint8_t XYT01_2_TX_PIN = 6;   // GP6 UART1 TX -> #2 RX
static const uint8_t XYT01_2_RX_PIN = 7;   // GP7 UART1 RX <- #2 TX
static const uint8_t XYT01_3_TX_PIN = 12;  // GP12 -> #3 RX
static const uint8_t XYT01_3_RX_PIN = 13;  // GP13 <- #3 TX
static const uint32_t XYT01_BAUD = 9600;

// Sensor I2C addresses (I2C1)
static const uint8_t SHT45_ADDR = 0x44;
static const uint8_t MS5611_ADDR = 0x77;   // 0x76 if CSB high
static const uint8_t ICM20948_ADDR_AD0_LOW = 0x69;
static const uint8_t ICM20948_ADDR_AD0_HIGH = 0x68;

// LMP91000
static const uint8_t LMP91000_ADDR = 0x48;