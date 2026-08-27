/*
 * Pico MicroSD (ADA254) Assembly Test
 * Connect the ADA254 MicroSD module (and power/GND) for this test.
 * Other SPI0 devices (MAX31865) may remain connected – their CS lines
 * keep them inactive.
 *
 * Pins (updated wiring table – ADA254 on SPI0):
 *   CS   = GP17
 *   MISO = GP16 (SPI0 RX)
 *   SCK  = GP18 (SPI0 SCK)
 *   MOSI = GP19 (SPI0 TX)
 *   5V   = VBUS / TB-5V
 *   GND  = TB-GND
 *
 * On success: onboard LED (GP25) blinks 3× (0.1 s on / 0.1 s off), then 1 s pause, forever.
 * On any failure: continuous fast blink (50 ms).
 */

#include <SPI.h>
#include <SD.h>

const int SD_CS   = 17;          // GP17
const int LED_PIN = LED_BUILTIN; // GP25 on Pico

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

  // Keep MAX31865 CS high so it stays inactive on the shared SPI0 bus
  pinMode(20, OUTPUT);
  digitalWrite(20, HIGH);

  Serial.begin(115200);
  delay(1000);
  Serial.println("MicroSD (ADA254) Test – starting...");

  // Explicitly set SPI0 pins
  SPI.setRX(16);   // MISO
  SPI.setTX(19);   // MOSI
  SPI.setSCK(18);  // SCK
  SPI.begin();

  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR: SD initialization failed!");
    errorBlink();
  }
  Serial.println("SD initialized OK.");

  // 1. Create folder "Test_Folder"
  if (!SD.exists("/Test_Folder")) {
    if (!SD.mkdir("/Test_Folder")) {
      Serial.println("ERROR: Failed to create Test_Folder");
      errorBlink();
    }
    Serial.println("Created /Test_Folder");
  } else {
    Serial.println("/Test_Folder already exists");
  }

  // Make the write deterministic (fresh file each boot)
  if (SD.exists("/Test_Folder/Test_File.txt")) {
    SD.remove("/Test_Folder/Test_File.txt");
  }

  // 2 + 3. Create Test_File.txt and write the test string
  File testFile = SD.open("/Test_Folder/Test_File.txt", FILE_WRITE);
  if (testFile) {
    testFile.println("Test successful.");
    testFile.close();
    Serial.println("Wrote \"Test successful.\" to /Test_Folder/Test_File.txt");
  } else {
    Serial.println("ERROR: Failed to open/create Test_File.txt");
    errorBlink();
  }

  // Optional but useful verification (read the file back)
  testFile = SD.open("/Test_Folder/Test_File.txt");
  if (testFile) {
    Serial.print("Verification – file contents: ");
    while (testFile.available()) {
      Serial.write(testFile.read());
    }
    testFile.close();
    Serial.println();
  } else {
    Serial.println("ERROR: Could not re-open file for verification");
    errorBlink();
  }

  Serial.println("Setup complete – entering success blink loop.");
}

void loop() {
  // 4. Blink onboard LED 3 times at 0.1-second intervals, then wait 1 second
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  delay(1000);
}
