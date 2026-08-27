/*
 * Pico MS5611 (GY-63) Assembly Test
 *
 * Pins (updated wiring table):
 *   SDA  = GP4  (TB-SDA / I2C0)
 *   SCL  = GP5  (TB-SCL / I2C0)
 *   5V   = VBUS / TB-5V
 *   GND  = TB-GND
 *
 * Library: MS5611 by Rob Tillaart
 *          (Library Manager → “MS5611”)
 *
 * Default I2C address on most GY-63 boards = 0x77
 * (change to 0x76 if your module’s CSB pin is pulled high)
 *
 * On success: continuous Serial samples (~1 Hz) containing
 *   timestamp_ms, temperature_C, pressure_mbar
 * On any failure (init / no response): continuous fast blink (50 ms)
 */

#include <Wire.h>
#include <MS5611.h>

// ---------- Pins ----------
const int LED_PIN = LED_BUILTIN; // GP25

// Most GY-63 boards default to 0x77
MS5611 ms5611(0x77);

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
  delay(1500);
  Serial.println(F("MS5611 (GY-63) Test – starting..."));

  // Explicit I2C0 pins
  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  Wire.setClock(100000);   // 100 kHz is safe and reliable

  if (!ms5611.begin()) {
    Serial.println(F("ERROR: MS5611 not found at 0x77"));
    Serial.println(F("Check wiring (SDA=GP4, SCL=GP5, 5V, GND) and address."));
    errorBlink();
  }

  // Optional: higher oversampling for better resolution (slower conversion)
  // ms5611.setOversampling(OSR_HIGH);   // or OSR_ULTRA_HIGH

  Serial.print(F("MS5611 found at 0x"));
  Serial.println(ms5611.getAddress(), HEX);
  Serial.println(F("Format: timestamp_ms,temperature_C,pressure_mbar"));
  Serial.println();
}

void loop() {
  // Trigger a conversion and wait for the result
  int result = ms5611.read();          // blocking read
  if (result != MS5611_READ_OK) {
    Serial.print(F("Read error: "));
    Serial.println(result);
    delay(500);
    return;
  }

  unsigned long ts = millis();         // timestamp of this measurement
  float temperature = ms5611.getTemperature();
  float pressure    = ms5611.getPressure();

  Serial.print(ts);
  Serial.print(',');
  Serial.print(temperature, 2);
  Serial.print(',');
  Serial.println(pressure, 2);

  delay(1000);   // ~1 Hz
}