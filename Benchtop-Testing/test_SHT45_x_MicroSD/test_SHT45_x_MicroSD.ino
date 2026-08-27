/*
 * Pico SHT45 + MicroSD (ADA254) Combined Assembly Test
 *
 * - Collects temperature, humidity and CRC status for a user-defined
 *   duration (COLLECTION_SECONDS)
 * - Writes every sample to /test_SHT45/SHT45_data.csv
 * - If the folder already exists it is deleted (including all contents)
 *   and recreated
 * - End of data collection is indicated via serial monitor + continuous
 *   success blink (3× 0.1 s on/off, then 1 s pause)
 *
 * Pins (updated wiring table):
 *   SHT45:
 *     SDA = GP4, SCL = GP5
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
 * Libraries: Adafruit SHT4x, SD, SPI
 *
 * On any failure: continuous 50 ms LED blink
 * On success: continuous successBlink
 */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_SHT4x.h>

// ---------- Pins ----------
const int LED_PIN = LED_BUILTIN; // GP25
const int SD_CS   = 17;          // GP17
const int MAX_CS  = 20;          // keep inactive on shared SPI0

// ---------- Timing ----------
const unsigned long COLLECTION_SECONDS = 20;                  // <-- change this
const unsigned long COLLECTION_MS      = 1000UL * COLLECTION_SECONDS;

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
File dataFile;

unsigned long collectionStart = 0;
bool collecting = false;
bool finished   = false;

void errorBlink() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
}

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
      Serial.print(F("Removed file: "));
      Serial.println(fullPath);
    }
  }
  dir.close();
}

bool prepareSD() {
  Serial.println(F("Initializing MicroSD (ADA254) on SPI0..."));

  // Keep MAX31865 CS high so it stays inactive on the shared bus
  pinMode(MAX_CS, OUTPUT);
  digitalWrite(MAX_CS, HIGH);

  // SPI0 pins
  SPI.setRX(16);   // MISO
  SPI.setTX(19);   // MOSI
  SPI.setSCK(18);  // SCK
  SPI.begin();

  if (!SD.begin(SD_CS)) {
    Serial.println(F("ERROR: SD initialization failed!"));
    return false;
  }
  Serial.println(F("SD initialized OK."));

  if (SD.exists("/test_SHT45")) {
    Serial.println(F("Removing existing /test_SHT45 ..."));
    removeFolderContents("/test_SHT45");
    if (!SD.rmdir("/test_SHT45")) {
      Serial.println(F("ERROR: Could not remove /test_SHT45"));
      return false;
    }
  }

  if (!SD.mkdir("/test_SHT45")) {
    Serial.println(F("ERROR: Failed to create /test_SHT45"));
    return false;
  }
  Serial.println(F("Created /test_SHT45"));

  dataFile = SD.open("/test_SHT45/SHT45_data.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println(F("ERROR: Failed to create SHT45_data.csv"));
    return false;
  }

  dataFile.println(F("timestamp_ms,temperature_C,humidity_%,crc_ok"));
  dataFile.flush();
  Serial.println(F("Created SHT45_data.csv with header"));
  return true;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Keep both CS lines as outputs and idle high
  pinMode(MAX_CS, OUTPUT);
  digitalWrite(MAX_CS, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  Serial.begin(115200);
  delay(1500);
  Serial.println(F("SHT45 + MicroSD (ADA254) Combined Test – starting..."));

  // ----- MicroSD first -----
  if (!prepareSD()) {
    errorBlink();
  }

  // ----- SHT45 -----
  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  Wire.setClock(100000);

  if (!sht4.begin(&Wire)) {
    Serial.println(F("ERROR: SHT45 not found at 0x44"));
    dataFile.close();
    errorBlink();
  }
  Serial.println(F("SHT45 found."));
  // Heater left OFF, precision left at library default.

  Serial.print(F(">>> "));
  Serial.print(COLLECTION_SECONDS);
  Serial.println(F("-Second Data collection BEGINNING NOW <<<"));
  collectionStart = millis();
  collecting = true;
}

void loop() {
  if (finished) {
    successBlink();
  }

  if (!collecting) return;

  // Stop after the configured duration
  if (millis() - collectionStart >= COLLECTION_MS) {
    collecting = false;
    finished   = true;

    dataFile.close();

    Serial.println(F(">>> Data collection ENDED <<<"));
    Serial.println(F("File closed. Data written to /test_SHT45/SHT45_data.csv"));

    successBlink();
  }

  // One sample
  sensors_event_t humidity, temp;
  uint32_t timestamp = millis();
  bool success = sht4.getEvent(&humidity, &temp);

  if (dataFile) {
    dataFile.print(timestamp);
    dataFile.print(',');

    if (success) {
      dataFile.print(temp.temperature, 2);
      dataFile.print(',');
      dataFile.print(humidity.relative_humidity, 2);
      dataFile.print(',');
      dataFile.println(F("1"));
    } else {
      dataFile.println(F("nan,nan,0"));
    }
  }

  delay(200);   // modest rate
}