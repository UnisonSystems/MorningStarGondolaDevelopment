/*
 * Pico BNO086 Assembly Test
 * Connect the BNO086 (and power/GND). MicroSD may remain connected (no pin conflict).
 *
 * Pins (matches wiring table):
 *   SDA  = GP4 (I2C0 SDA / TB-SDA)
 *   SCL  = GP5 (I2C0 SCL / TB-SCL)
 *   INT  = GP6 (active-low)
 *   RST  = GP7 (active-low)
 *   3V3  = TB-3V3
 *   GND  = TB-GND
 *
 * Requires: Adafruit BNO08x library (+ Adafruit BusIO / Unified Sensor)
 *
 * - Calibrated 9-DoF (accel + gyro + mag) at 100 Hz
 * - Report timestamps (library value derived from base timebase + delay)
 * - One combined sample printed to Serial every second
 *
 * On success: continuous Serial samples every 1 s.
 * On any failure: continuous fast blink (50 ms) of onboard LED (GP25).
 */

#include <Wire.h>
#include <Adafruit_BNO08x.h>

const int LED_PIN = LED_BUILTIN; // GP25 on Pico
const int BNO_RST = 7;           // GP7

#define BNO08X_ADDR_PRIMARY   0x4B
#define BNO08X_ADDR_SECONDARY 0x4A

Adafruit_BNO08x bno08x(BNO_RST);
sh2_SensorValue_t sensorValue;

// Cached latest values
float   accelX = 0, accelY = 0, accelZ = 0;
uint8_t accelStatus = 0;
float   gyroX  = 0, gyroY  = 0, gyroZ  = 0;
uint8_t gyroStatus  = 0;
float   magX   = 0, magY   = 0, magZ   = 0;
uint8_t magStatus   = 0;
uint64_t reportTimestamp = 0;
uint32_t reportDelay     = 0;

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

void setReports() {
  Serial.println(F("Enabling calibrated reports at 100 Hz..."));
  if (!bno08x.enableReport(SH2_ACCELEROMETER, 10000)) {
    Serial.println(F("Could not enable SH2_ACCELEROMETER"));
  }
  if (!bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000)) {
    Serial.println(F("Could not enable SH2_GYROSCOPE_CALIBRATED"));
  }
  if (!bno08x.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED, 10000)) {
    Serial.println(F("Could not enable SH2_MAGNETIC_FIELD_CALIBRATED"));
  }
  Serial.println(F("Reports enabled."));
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(1000);
  Serial.println(F("BNO086 Test – starting..."));

  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  Wire.setClock(100000);

  bool found = false;
  if (bno08x.begin_I2C(BNO08X_ADDR_PRIMARY, &Wire)) {
    Serial.print(F("BNO08x found at 0x"));
    Serial.println(BNO08X_ADDR_PRIMARY, HEX);
    found = true;
  } else if (bno08x.begin_I2C(BNO08X_ADDR_SECONDARY, &Wire)) {
    Serial.print(F("BNO08x found at 0x"));
    Serial.println(BNO08X_ADDR_SECONDARY, HEX);
    found = true;
  }

  if (!found) {
    Serial.println(F("ERROR: BNO08x not detected on I2C. Check wiring, address, 3V3 and GND."));
    errorBlink();
  }

  Serial.println(F("BNO08x initialized OK."));
  setReports();
  Serial.println(F("Setup complete – collecting data at 100 Hz, printing one sample every second."));
  lastPrintMillis = millis();
}

void loop() {
  if (bno08x.wasReset()) {
    Serial.println(F("Sensor was reset – re-enabling reports"));
    setReports();
  }

  if (bno08x.getSensorEvent(&sensorValue)) {
    reportTimestamp = sensorValue.timestamp;
    reportDelay     = sensorValue.delay;

    switch (sensorValue.sensorId) {
      case SH2_ACCELEROMETER:
        accelX = sensorValue.un.accelerometer.x;
        accelY = sensorValue.un.accelerometer.y;
        accelZ = sensorValue.un.accelerometer.z;
        accelStatus = sensorValue.status;
        break;

      case SH2_GYROSCOPE_CALIBRATED:
        gyroX = sensorValue.un.gyroscope.x;
        gyroY = sensorValue.un.gyroscope.y;
        gyroZ = sensorValue.un.gyroscope.z;
        gyroStatus = sensorValue.status;
        break;

      case SH2_MAGNETIC_FIELD_CALIBRATED:
        magX = sensorValue.un.magneticField.x;
        magY = sensorValue.un.magneticField.y;
        magZ = sensorValue.un.magneticField.z;
        magStatus = sensorValue.status;
        break;

      default:
        break;
    }
  }

  if (millis() - lastPrintMillis >= PRINT_INTERVAL_MS) {
    lastPrintMillis = millis();

    Serial.println(F("----- BNO086 Sample -----"));
    Serial.print(F("Timestamp (us): "));
    Serial.println(reportTimestamp);
    Serial.print(F("Delay (us): "));
    Serial.println(reportDelay);

    Serial.print(F("Accel (m/s^2)  X: ")); Serial.print(accelX, 4);
    Serial.print(F("  Y: ")); Serial.print(accelY, 4);
    Serial.print(F("  Z: ")); Serial.print(accelZ, 4);
    Serial.print(F("  Acc: ")); Serial.println(accelStatus);

    Serial.print(F("Gyro  (rad/s)  X: ")); Serial.print(gyroX, 4);
    Serial.print(F("  Y: ")); Serial.print(gyroY, 4);
    Serial.print(F("  Z: ")); Serial.print(gyroZ, 4);
    Serial.print(F("  Acc: ")); Serial.println(gyroStatus);

    Serial.print(F("Mag   (uT)     X: ")); Serial.print(magX, 4);
    Serial.print(F("  Y: ")); Serial.print(magY, 4);
    Serial.print(F("  Z: ")); Serial.print(magZ, 4);
    Serial.print(F("  Acc: ")); Serial.println(magStatus);

    Serial.println(F("-------------------------"));
  }
}