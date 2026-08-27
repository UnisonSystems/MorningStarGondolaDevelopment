/*
 * Pico SHT45 Assembly Test
 * Connect the SHT45 (and power/GND) for this test.
 * Other I2C0 devices may remain connected.
 *
 * Pins (updated wiring table):
 *   SDA  = GP4  (TB-SDA / I2C0)
 *   SCL  = GP5  (TB-SCL / I2C0)
 *   3V3  = TB-3V3
 *   GND  = TB-GND
 *
 * Library: Adafruit SHT4x  (+ Adafruit BusIO)
 *          Library Manager → “Adafruit SHT4x”
 *
 * Default I2C address = 0x44
 * Heater remains OFF, precision left at library default.
 *
 * On success: continuous Serial samples (~1 Hz) containing
 *   timestamp_ms, temperature_C, humidity_%, crc_ok
 * On any failure (init / no response): continuous fast blink (50 ms)
 */

#include <Wire.h>
#include <Adafruit_SHT4x.h>

// ---------- Pins ----------
const int LED_PIN = LED_BUILTIN; // GP25

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

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
  Serial.println(F("SHT45 Test – starting..."));

  // Explicit I2C0 pins
  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  Wire.setClock(100000);

  if (!sht4.begin(&Wire)) {
    Serial.println(F("ERROR: SHT45 not found at 0x44"));
    Serial.println(F("Check wiring (SDA=GP4, SCL=GP5, 3V3, GND) and library install."));
    errorBlink();
  }

  Serial.println(F("SHT45 found."));
  // Heater left OFF (library default) and precision left at library default.

  Serial.println(F("Format: timestamp_ms,temperature_C,humidity_%,crc_ok"));
  Serial.println();
}

void loop() {
  sensors_event_t humidity, temp;

  uint32_t timestamp = millis();
  bool success = sht4.getEvent(&humidity, &temp);   // true only if transaction + CRC succeeded

  Serial.print(timestamp);
  Serial.print(',');

  if (success) {
    Serial.print(temp.temperature, 2);
    Serial.print(',');
    Serial.print(humidity.relative_humidity, 2);
    Serial.print(',');
    Serial.println(F("1"));
  } else {
    Serial.println(F("nan,nan,0"));
  }

  delay(1000);   // ~1 Hz
}