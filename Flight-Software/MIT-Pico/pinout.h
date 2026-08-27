#pragma once

#include <Arduino.h>

// Pins (pico_wiring.txt)
static const uint8_t XY1_TX_PIN = 0;       // GP0 UART0 TX
static const uint8_t XY1_RX_PIN = 1;       // GP1 UART0 RX
static const uint8_t I2C1_SDA_PIN = 2;     // GP2 Tiny4FSK SDA
static const uint8_t I2C1_SCL_PIN = 3;     // GP3 Tiny4FSK SCL
static const uint8_t I2C0_SDA_PIN = 4;     // GP4 sensor SDA
static const uint8_t I2C0_SCL_PIN = 5;     // GP5 sensor SCL
static const uint8_t BNO_INT_PIN = 6;      // GP6 wired, unused in software
static const uint8_t BNO_RST = 7;          // GP7
static const uint8_t XY2_TX_PIN = 8;       // GP8 UART1 TX
static const uint8_t XY2_RX_PIN = 9;       // GP9 UART1 RX
static const uint8_t MOSFET_PIN = 10;      // GP10 digital enable
static const uint8_t LED_STATUS = 15;      // GP15
static const uint8_t SPI0_RX_PIN = 16;     // GP16 MISO
static const uint8_t SD_CS = 17;           // GP17
static const uint8_t SPI0_SCK_PIN = 18;    // GP18
static const uint8_t SPI0_TX_PIN = 19;     // GP19 MOSI
static const uint8_t MAX_CS = 20;          // GP20
static const uint8_t LED_ONBOARD = LED_BUILTIN; // GP25

// I2C
static const uint8_t TINY4FSK_SLAVE_ADDR = 0x09;
static const uint32_t I2C1_FREQ = 400000;
static const uint32_t I2C0_FREQ = 100000;
static const uint8_t BNO086_ADDR = 0x4B;
static const uint8_t MS5611_ADDR = 0x77;

// UART
static const uint32_t XY_BAUD = 9600;
static const uint32_t USB_SERIAL_BAUD = 115200;

// MAX31865
static const float MAX_RREF = 430.0f;
static const float MAX_RNOMINAL = 100.0f;
static const uint32_t MAX_SPI_HZ = 500000;

// Timing
static const unsigned long ALIVE_BLINK_MS = 10000;
static const unsigned long BNO_INTERVAL_US = 10000;
static const unsigned long SUITE_INTERVAL_US = 50000;
static const unsigned long FLUSH_INTERVAL_MS = 2000;

// Buffer sizes
static const size_t BUFFER_SIZE = 24 * 1024;
static const size_t FLUSH_THRESHOLD = (BUFFER_SIZE * 80) / 100;
static const size_t XY_LINE_BUF_SIZE = 40;
static const size_t LOG_FILENAME_SIZE = 32;
