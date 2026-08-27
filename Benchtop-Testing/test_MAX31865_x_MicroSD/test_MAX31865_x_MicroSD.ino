/*
 * Pico MAX31865 + MicroSD (ADA254) Combined Assembly Test
 *
 * - Collects temperature, RTD resistance, raw RTD value, ratio and fault status
 *   for exactly 5 seconds
 * - Writes every sample to /test_MAX31865/MAX31865_data.csv
 * - If the folder already exists it is deleted (including all contents) and recreated
 * - End of data collection is indicated via serial monitor + continuous success blink
 *
 * Pins (updated wiring table – both devices on shared SPI0):
 *   MAX31865:
 *     CS   = GP20
 *     MISO = GP16 (SPI0 RX)
 *     SCK  = GP18 (SPI0 SCK)
 *     MOSI = GP19 (SPI0 TX)
 *     5V / GND
 *   MicroSD (ADA254):
 *     CS   = GP17
 *     MISO = GP16 (SPI0 RX)
 *     SCK  = GP18 (SPI0 SCK)
 *     MOSI = GP19 (SPI0 TX)
 *     5V / GND
 *
 * Libraries: Adafruit MAX31865, SD, SPI
 *
 * On success: continuous triple-blink (0.1 s on/off) then 1 s pause
 * On any failure: continuous fast blink (50 ms)
 */

#include <SPI.h>
#include <SD.h>
#include <Adafruit_MAX31865.h>

// ---------- Pins ----------
const int LED_PIN = LED_BUILTIN; // GP25
const int MAX_CS  = 20;          // GP20
const int SD_CS   = 17;          // GP17

// ---------- RTD configuration ----------
#define RREF      430.0
#define RNOMINAL  100.0

Adafruit_MAX31865 max31865(MAX_CS);   // Hardware SPI0

File dataFile;

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

  // Ensure MAX31865 CS is high before talking to the SD card
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
  if (SD.exists("/test_MAX31865")) {
    Serial.println("Removing existing /test_MAX31865 ...");
    removeFolderContents("/test_MAX31865");
    if (!SD.rmdir("/test_MAX31865")) {
      Serial.println("ERROR: Could not remove /test_MAX31865");
      return false;
    }
  }

  // Create fresh folder
  if (!SD.mkdir("/test_MAX31865")) {
    Serial.println("ERROR: Failed to create /test_MAX31865");
    return false;
  }
  Serial.println("Created /test_MAX31865");

  // Create CSV and write header
  dataFile = SD.open("/test_MAX31865/MAX31865_data.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println("ERROR: Failed to create MAX31865_data.csv");
    return false;
  }

  dataFile.println("timestamp_ms,temperature_C,resistance_ohm,raw_rtd,ratio,fault");
  dataFile.flush();
  Serial.println("Created MAX31865_data.csv with header");
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
  Serial.println("MAX31865 + MicroSD (ADA254) Combined Test – starting...");

  // ----- MicroSD first -----
  if (!prepareSD()) {
    errorBlink();
  }

  // ----- MAX31865 -----
  // (SPI0 already configured by prepareSD)
  digitalWrite(SD_CS, HIGH);          // make sure SD is deselected
  digitalWrite(MAX_CS, HIGH);

  if (!max31865.begin(MAX31865_3WIRE)) {
    Serial.println("ERROR: MAX31865 initialization failed!");
    dataFile.close();
    errorBlink();
  }
  Serial.println("MAX31865 initialized OK (PT100 / 430 Ω RREF / 3-wire).");
  max31865.clearFault();
  max31865.enableBias(true);

  // Notify start of collection
  Serial.println(">>> 5-Second Data collection BEGINNING NOW <<<");
  collectionStart = millis();
  collecting = true;
}

void loop() {
  if (finished) {
    successBlink();
  }

  if (!collecting) return;

  // Stop after 5 seconds
  if (millis() - collectionStart >= COLLECTION_MS) {
    collecting = false;
    finished = true;

    dataFile.close();

    Serial.println(">>> Data collection ENDED <<<");
    Serial.println("File closed. Data written to /test_MAX31865/MAX31865_data.csv");

    successBlink();
  }

  // One sample from the MAX31865
  digitalWrite(SD_CS, HIGH);          // deselect SD
  digitalWrite(MAX_CS, LOW);          // (library manages CS, but be explicit)

  uint16_t rtd = max31865.readRTD();
  float ratio = rtd / 32768.0f;
  float resistance = RREF * ratio;
  float temperature = max31865.calculateTemperature(rtd, RNOMINAL, RREF);
  uint8_t fault = max31865.readFault();
  if (fault) max31865.clearFault();

  digitalWrite(MAX_CS, HIGH);

  // Write a complete line
  if (dataFile) {
    dataFile.print(millis() - collectionStart);
    dataFile.print(',');
    dataFile.print(temperature, 2);
    dataFile.print(',');
    dataFile.print(resistance, 4);
    dataFile.print(',');
    dataFile.print(rtd);
    dataFile.print(',');
    dataFile.print(ratio, 6);
    dataFile.print(',');
    dataFile.println(fault);
  }
}
