/*
 * Pico MAX31865 Assembly Test
 * Connect only the MAX31865 module (and power/GND) for this test.
 * MicroSD may remain connected (no pin conflict on CS).
 *
 * Pins (matches wiring table):
 *   CS   = GP20
 *   MISO = GP16 (SPI0 RX)
 *   SCK  = GP18 (SPI0 SCK)
 *   MOSI = GP19 (SPI0 TX)
 *   5V   = VBUS / TB-5V
 *   GND  = TB-GND
 *
 * Requires: Adafruit MAX31865 library (+ Adafruit BusIO)
 *
 * Configuration (edit the #defines below if your RTD differs):
 *   - PT100 (RNOMINAL = 100.0 Ω at 0 °C)
 *   - Reference resistor RREF = 430.0 Ω
 *   - 3-wire RTD (MAX31865_3WIRE). Change to MAX31865_2WIRE or MAX31865_4WIRE if needed.
 *
 * On success: continuous Serial samples every 1 second containing:
 *   1. Temperature (°C)
 *   2. RTD resistance (Ω) together with raw RTD value / ratio
 *   3. Fault status (hex + decoded description if any)
 * On any failure (initialization / no response from the chip): continuous fast blink
 *   (50 ms) of the onboard LED (GP25).
 */

#include <SPI.h>
#include <Adafruit_MAX31865.h>

// ---------- Pins ----------
const int LED_PIN = LED_BUILTIN; // GP25 on Pico
const int MAX_CS  = 20;          // GP20
const int ADA254_CS = 17;

// ---------- RTD configuration (edit if your hardware differs) ----------
// The value of the Rref resistor on the MAX31865 board.
// Use 430.0 for PT100 and 4300.0 for PT1000.
#define RREF      430.0
// The 'nominal' 0-degrees-C resistance of the sensor.
// 100.0 for PT100, 1000.0 for PT1000.
#define RNOMINAL  100.0

Adafruit_MAX31865 max31865(MAX_CS);   // Hardware SPI, CS pin only

unsigned long lastPrintMillis = 0;
const unsigned long PRINT_INTERVAL_MS = 1000;

void errorBlink() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(1000);                   // brief wait so USB serial can attach if present
  Serial.println(F("MAX31865 Test – starting..."));

  // Explicitly set SPI0 pins (matches MicroSD test + wiring table)
  SPI.setRX(16);   // MISO
  SPI.setTX(19);   // MOSI
  SPI.setSCK(18);  // SCK
  SPI.begin();
  SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));
  // CS is managed by the Adafruit_MAX31865 library

  if (!max31865.begin(MAX31865_3WIRE)) {
    Serial.println(F("ERROR: MAX31865 initialization failed!"));
    Serial.println(F("Check wiring (CS=GP20, MISO/SCK/MOSI, 5V, GND) and library install."));
    errorBlink();
  }
  Serial.println(F("MAX31865 initialized OK (PT100 / 430 Ω RREF / 3-wire)."));
  max31865.clearFault();          // clear any power-on faults

  Serial.println(F("Setup complete – printing one sample every second."));
  Serial.println(F("(Edit RREF / RNOMINAL / wire mode in source if your RTD differs.)"));
  Serial.println();
  lastPrintMillis = millis();
}

void loop() {
  if (millis() - lastPrintMillis >= PRINT_INTERVAL_MS) {
    lastPrintMillis = millis();

    // 1. Read raw 16-bit RTD value (library performs the conversion cycle)
    uint16_t rtd = max31865.readRTD();

    // Convert to ratio and resistance
    float ratio = rtd / 32768.0f;
    float resistance = RREF * ratio;

    // 2. Temperature in °C
    float temperature = max31865.temperature(RNOMINAL, RREF);

    // 3. Fault status
    uint8_t fault = max31865.readFault();

    // ----- Print sample (matches requested outputs) -----
    Serial.println(F("----- MAX31865 Sample -----"));

    Serial.print(F("Temperature: "));
    Serial.print(temperature, 2);
    Serial.println(F(" °C"));

    Serial.print(F("RTD Resistance: "));
    Serial.print(resistance, 4);
    Serial.println(F(" Ω"));

    Serial.print(F("Raw RTD: "));
    Serial.print(rtd);
    Serial.print(F("  (ratio: "));
    Serial.print(ratio, 6);
    Serial.println(F(")"));

    Serial.print(F("Fault status: 0x"));
    if (fault < 0x10) Serial.print(F("0"));
    Serial.print(fault, HEX);

    if (fault) {
      Serial.println(F("  [FAULTS DETECTED]"));
      if (fault & MAX31865_FAULT_HIGHTHRESH) {
        Serial.println(F("  - RTD High Threshold"));
      }
      if (fault & MAX31865_FAULT_LOWTHRESH) {
        Serial.println(F("  - RTD Low Threshold"));
      }
      if (fault & MAX31865_FAULT_REFINLOW) {
        Serial.println(F("  - REFIN- > 0.85 x Bias"));
      }
      if (fault & MAX31865_FAULT_REFINHIGH) {
        Serial.println(F("  - REFIN- < 0.85 x Bias - FORCE- open"));
      }
      if (fault & MAX31865_FAULT_RTDINLOW) {
        Serial.println(F("  - RTDIN- < 0.85 x Bias - FORCE- open"));
      }
      if (fault & MAX31865_FAULT_OVUV) {
        Serial.println(F("  - Under/Over voltage"));
      }
      max31865.clearFault();
    } else {
      Serial.println(F("  (none)"));
    }

    Serial.println(F("---------------------------"));
    Serial.println();
  }
}