/*
 * Pico BNO086 + MicroSD (ADA254) Combined Assembly Test
 *
 * - Collects calibrated Accel / Gyro / Mag + timestamps for exactly 5 seconds
 * - Writes every sample (as collected) to /BNO086_Test/BNO086_data.csv
 * - If the folder already exists it is deleted (including all contents) and recreated
 * - End of data collection is indicated via serial monitor + triple blinks on the Pico
 *
 * Pins (updated wiring table – ADA254 on SPI0):
 *   BNO086:
 *     SDA = GP4, SCL = GP5, INT = GP6, RST = GP7
 *     3V3 / GND
 *   MicroSD (ADA254 on SPI0):
 *     CS   = GP17
 *     MISO = GP16 (SPI0 RX)
 *     SCK  = GP18 (SPI0 SCK)
 *     MOSI = GP19 (SPI0 TX)
 *     5V / GND
 *
 * Note: MAX31865 may remain connected (its CS = GP20 is held high).
 *
 * Libraries: Adafruit BNO08x, SD, SPI
 */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_BNO08x.h>

// ---------- Pins ----------
const int LED_PIN   = LED_BUILTIN; // GP25
const int BNO_RST   = 7;
const int SD_CS     = 17;
const int MAX_CS    = 20;          // keep inactive on shared SPI0

#define BNO08X_ADDR 0x4B

// ---------- Objects ----------
Adafruit_BNO08x bno08x(BNO_RST);
sh2_SensorValue_t sensorValue;

File dataFile;

// Cached sensor values
float   accelX = 0, accelY = 0, accelZ = 0;
float   gyroX  = 0, gyroY  = 0, gyroZ  = 0;
float   magX   = 0, magY   = 0, magZ   = 0;
uint64_t reportTimestamp = 0;

unsigned long collectionStart = 0;
const unsigned long COLLECTION_MS = 5000;   // 5 seconds
bool collecting = false;
bool finished = false;

void errorBlink() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
}

// Success pattern: 3× (0.1 s on / 0.1 s off), then 1 s pause, forever
void successBlink() {
  while (true) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
    delay(1000);
  }
}

// ---------- SD helpers ----------
void removeFolderContents(const char *path) {
  File dir = SD.open(path);
  if (!dir) return;

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry.name());

    if (entry.isDirectory()) {
      entry.close();
    } else {
      entry.close();
      SD.remove(fullPath);
      Serial.print("Removed file: ");
      Serial.println(fullPath);
    }
  }
  dir.close();
}

bool prepareSD() {
  Serial.println("Initializing MicroSD (ADA254) on SPI0...");

  // Keep MAX31865 CS high so it stays inactive on the shared bus
  pinMode(MAX_CS, OUTPUT);
  digitalWrite(MAX_CS, HIGH);

  // SPI0 pins
  SPI.setRX(16);   // MISO
  SPI.setTX(19);   // MOSI
  SPI.setSCK(18);  // SCK
  SPI.begin();

  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR: SD initialization failed!");
    return false;
  }
  Serial.println("SD initialized OK.");

  // Delete existing folder + contents
  if (SD.exists("/BNO086_Test")) {
    Serial.println("Removing existing /BNO086_Test ...");
    removeFolderContents("/BNO086_Test");
    if (!SD.rmdir("/BNO086_Test")) {
      Serial.println("ERROR: Could not remove /BNO086_Test");
      return false;
    }
  }

  // Create fresh folder
  if (!SD.mkdir("/BNO086_Test")) {
    Serial.println("ERROR: Failed to create /BNO086_Test");
    return false;
  }
  Serial.println("Created /BNO086_Test");

  // Create CSV and write header
  dataFile = SD.open("/BNO086_Test/BNO086_data.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println("ERROR: Failed to create BNO086_data.csv");
    return false;
  }

  dataFile.println("timestamp_us,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,mag_x,mag_y,mag_z");
  dataFile.flush();
  Serial.println("Created BNO086_data.csv with header");
  return true;
}

// ---------- BNO helpers ----------
void setReports() {
  Serial.println("Enabling calibrated reports at 100 Hz...");
  bno08x.enableReport(SH2_ACCELEROMETER, 10000);
  bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000);
  bno08x.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED, 10000);
  Serial.println("Reports enabled.");
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(1500);
  Serial.println("BNO086 + MicroSD (ADA254) Combined Test – starting...");

  // ----- MicroSD first -----
  if (!prepareSD()) {
    errorBlink();
  }

  // ----- BNO086 -----
  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  Wire.setClock(100000);

  if (!bno08x.begin_I2C(BNO08X_ADDR, &Wire)) {
    Serial.println("ERROR: BNO08x not detected at 0x4B");
    dataFile.close();
    errorBlink();
  }
  Serial.println("BNO08x found!");

  setReports();

  // Notify start of collection
  Serial.println(">>> 5-Second Data collection BEGINNING NOW <<<");
  collectionStart = millis();
  collecting = true;
}

void loop() {
  if (finished) {
    // Should never reach here, but safety
    successBlink();
  }

  if (!collecting) return;

  // Stop after 5 seconds
  if (millis() - collectionStart >= COLLECTION_MS) {
    collecting = false;
    finished = true;

    dataFile.close();

    // Notify end of collection
    Serial.println(">>> Data collection ENDED <<<");
    Serial.println("File closed. Data written to /BNO086_Test/BNO086_data.csv");

    // Continuous success blink pattern
    successBlink();
  }

  // Handle sensor reset
  if (bno08x.wasReset()) {
    Serial.println("Sensor was reset – re-enabling reports");
    setReports();
  }

  // Poll BNO events and write immediately
  if (bno08x.getSensorEvent(&sensorValue)) {
    reportTimestamp = sensorValue.timestamp;

    switch (sensorValue.sensorId) {
      case SH2_ACCELEROMETER:
        accelX = sensorValue.un.accelerometer.x;
        accelY = sensorValue.un.accelerometer.y;
        accelZ = sensorValue.un.accelerometer.z;
        break;
      case SH2_GYROSCOPE_CALIBRATED:
        gyroX = sensorValue.un.gyroscope.x;
        gyroY = sensorValue.un.gyroscope.y;
        gyroZ = sensorValue.un.gyroscope.z;
        break;
      case SH2_MAGNETIC_FIELD_CALIBRATED:
        magX = sensorValue.un.magneticField.x;
        magY = sensorValue.un.magneticField.y;
        magZ = sensorValue.un.magneticField.z;
        break;
      default:
        break;
    }

    // Write a complete line with the latest values
    if (dataFile) {
      dataFile.print(reportTimestamp);
      dataFile.print(',');
      dataFile.print(accelX, 4); dataFile.print(',');
      dataFile.print(accelY, 4); dataFile.print(',');
      dataFile.print(accelZ, 4); dataFile.print(',');
      dataFile.print(gyroX, 4);  dataFile.print(',');
      dataFile.print(gyroY, 4);  dataFile.print(',');
      dataFile.print(gyroZ, 4);  dataFile.print(',');
      dataFile.print(magX, 4);   dataFile.print(',');
      dataFile.print(magY, 4);   dataFile.print(',');
      dataFile.println(magZ, 4);
    }
  }
}
