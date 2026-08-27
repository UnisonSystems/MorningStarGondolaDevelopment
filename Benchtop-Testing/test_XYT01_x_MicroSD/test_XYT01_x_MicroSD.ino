/*
 * Pico XY-T01 + MicroSD (ADA254) Combined Assembly Test
 *
 * - Collects temperature + parse-time timestamps from both XY-T01 units
 *   for a user-defined duration (COLLECTION_SECONDS)
 * - Writes every sample to /test_XYT01/XYT01_data.csv
 * - If the folder already exists it is deleted (including all contents)
 *   and recreated
 * - At the start of the test both units are set to 100 °C.
 *   They remain at 100 °C for min(10 s, collection duration).
 *   After that time they are set to 0 °C for the remainder of the test.
 * - End of data collection is indicated via serial monitor + a special
 *   setpoint sequence on both controllers: 0 °C (1 s) → 100 °C (1 s) → 0 °C
 *
 * Pins (updated wiring table – ADA254 on SPI0):
 *   XY-T01 #1 : UART0  GP0 (TX) / GP1 (RX)
 *   XY-T01 #2 : UART1  GP8 (TX) / GP9 (RX)
 *   MicroSD (ADA254 on SPI0):
 *     CS   = GP17
 *     MISO = GP16 (SPI0 RX)
 *     SCK  = GP18 (SPI0 SCK)
 *     MOSI = GP19 (SPI0 TX)
 *     5V / GND
 *
 * Note: MAX31865 may remain connected (its CS = GP20 is held high).
 *
 * Libraries: SD, SPI
 *
 * On any failure: continuous 50 ms LED blink
 * On success: the setpoint sequence above + continuous successBlink
 */

#include <SPI.h>
#include <SD.h>

// ---------- Pins ----------
const int LED_PIN = LED_BUILTIN; // GP25
const int SD_CS   = 17;          // GP17
const int MAX_CS  = 20;          // keep inactive on shared SPI0

// ---------- Timing ----------
const unsigned long COLLECTION_SECONDS = 8;                 
const unsigned long COLLECTION_MS      = 1000UL * COLLECTION_SECONDS;
const unsigned long PRINT_INTERVAL_MS  = 1000;               // match ~1 Hz XY-T01 report rate
const unsigned long SETPOINT_HOLD_MS   = 10000;              // hold 100 °C for up to 10 s

unsigned long collectionStart   = 0;
unsigned long lastPrintMillis   = 0;
unsigned long setpointHoldUntil = 0;   // when to switch from 100 °C → 0 °C
bool collecting     = false;
bool finished       = false;
bool setpointIsHigh = true;            // currently commanding 100 °C?

// Latest data (timestamp = millis() when the value was parsed)
float         temp1 = NAN;
unsigned long ts1   = 0;
float         temp2 = NAN;
unsigned long ts2   = 0;

File dataFile;

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

// ---------- helpers ----------
float extractTemperature(const String &line) {
  int i = 0;
  while (i < (int)line.length() &&
         !(isDigit(line[i]) || line[i] == '-' || line[i] == '.')) {
    i++;
  }
  if (i >= (int)line.length()) return NAN;

  String num;
  while (i < (int)line.length() &&
         (isDigit(line[i]) || line[i] == '.' || line[i] == '-')) {
    num += line[i];
    i++;
  }
  if (num.length() == 0) return NAN;
  return num.toFloat();
}

void sendCommand(HardwareSerial &port, const char *cmd) {
  port.print(cmd);   // no terminator – required by these modules
  port.flush();
}

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

  if (SD.exists("/test_XYT01")) {
    Serial.println(F("Removing existing /test_XYT01 ..."));
    removeFolderContents("/test_XYT01");
    if (!SD.rmdir("/test_XYT01")) {
      Serial.println(F("ERROR: Could not remove /test_XYT01"));
      return false;
    }
  }

  if (!SD.mkdir("/test_XYT01")) {
    Serial.println(F("ERROR: Failed to create /test_XYT01"));
    return false;
  }
  Serial.println(F("Created /test_XYT01"));

  dataFile = SD.open("/test_XYT01/XYT01_data.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println(F("ERROR: Failed to create XYT01_data.csv"));
    return false;
  }

  dataFile.println(F("ts1_ms,temp1_C,ts2_ms,temp2_C"));
  dataFile.flush();
  Serial.println(F("Created XYT01_data.csv with header"));
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
  Serial.println(F("XY-T01 + MicroSD (ADA254) Combined Test – starting..."));

  // ----- MicroSD (SPI0) -----
  if (!prepareSD()) {
    errorBlink();
  }

  // ----- UARTs -----
  Serial1.setTX(0);
  Serial1.setRX(1);
  Serial1.begin(9600);
  delay(50);

  Serial2.setTX(8);
  Serial2.setRX(9);
  Serial2.begin(9600);
  delay(50);

  // Enable relays and start reporting
  sendCommand(Serial1, "ON");
  sendCommand(Serial2, "ON");
  delay(100);
  sendCommand(Serial1, "start");
  sendCommand(Serial2, "start");
  delay(200);

  // Set both units to 100 °C
  sendCommand(Serial1, "S:100");
  sendCommand(Serial2, "S:100");
  setpointIsHigh = true;

  // Decide how long to keep the relays closed
  // (min of 10 s or the full collection duration)
  unsigned long holdMs = (COLLECTION_MS < SETPOINT_HOLD_MS) ? COLLECTION_MS : SETPOINT_HOLD_MS;
  setpointHoldUntil = millis() + holdMs;

  Serial.print(F(">>> "));
  Serial.print(COLLECTION_SECONDS);
  Serial.println(F("-Second Data collection BEGINNING NOW <<<"));
  collectionStart = millis();
  lastPrintMillis = millis();
  collecting = true;
}

void loop() {
  if (finished) {
    successBlink();
  }

  if (!collecting) return;

  // ---- Continuously drain both UARTs ----
  while (Serial1.available()) {
    String line = Serial1.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      float t = extractTemperature(line);
      if (!isnan(t)) {
        temp1 = t;
        ts1   = millis();
      }
    }
  }
  while (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      float t = extractTemperature(line);
      if (!isnan(t)) {
        temp2 = t;
        ts2   = millis();
      }
    }
  }

  // ---- Switch from 100 °C → 0 °C after the hold period ----
  if (setpointIsHigh && (millis() >= setpointHoldUntil)) {
    sendCommand(Serial1, "S:000");
    sendCommand(Serial2, "S:000");
    setpointIsHigh = false;
  }

  // ---- Write one line per second during the collection window ----
  if (millis() - lastPrintMillis >= PRINT_INTERVAL_MS) {
    lastPrintMillis = millis();

    if (dataFile) {
      if (ts1 == 0) dataFile.print(F("0,nan"));
      else {
        dataFile.print(ts1);
        dataFile.print(',');
        dataFile.print(temp1, 1);
      }
      dataFile.print(',');

      if (ts2 == 0) dataFile.print(F("0,nan"));
      else {
        dataFile.print(ts2);
        dataFile.print(',');
        dataFile.print(temp2, 1);
      }
      dataFile.println();
    }
  }

  // ---- End of collection ----
  if (millis() - collectionStart >= COLLECTION_MS) {
    collecting = false;
    finished   = true;

    dataFile.close();

    Serial.println(F(">>> Data collection ENDED <<<"));
    Serial.println(F("File closed. Data written to /test_XYT01/XYT01_data.csv"));

    // Special success indication: 0 °C (1 s) → 100 °C (1 s) → 0 °C
    sendCommand(Serial1, "S:000");
    sendCommand(Serial2, "S:000");
    delay(1000);
    sendCommand(Serial1, "S:100");
    sendCommand(Serial2, "S:100");
    delay(1000);
    sendCommand(Serial1, "S:000");
    sendCommand(Serial2, "S:000");

    successBlink();
  }
}