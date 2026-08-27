/*
 * Pico XY-T01 Assembly Test (two units)
 *
 * - XY-T01 #1 on UART0 (GP0 TX / GP1 RX) via level shifter
 * - XY-T01 #2 on UART1 (GP8 TX / GP9 RX) via level shifter
 *
 * Behavior:
 *   1. Starts continuous temperature reporting on both controllers.
 *   2. Continuously drains both UARTs.  When a new temperature is
 *      successfully parsed, the value and the millis() timestamp of
 *      that moment are stored.
 *   3. Prints a line once per second containing the most recent
 *      (timestamp, temperature) pair for each unit.
 *   4. Toggles the setpoint of both units between 100 °C and 0 °C every
 *      3 seconds (starts at 100 °C).
 *
 * Communication: 9600 8N1
 * Commands used: "start", "ON", "S:100", "S:000"
 *
 * Output format (CSV-style):
 *   ts1_ms,temp1_C,ts2_ms,temp2_C
 */

#include <Arduino.h>

// ---------- Pins ----------
const int LED_PIN = LED_BUILTIN;   // GP25

// ---------- Timing ----------
const unsigned long PRINT_INTERVAL_MS    = 1000;  // match ~1 Hz report rate
const unsigned long SETPOINT_INTERVAL_MS = 3000;  // 3 s toggle

unsigned long lastPrintMillis    = 0;
unsigned long lastSetpointMillis = 0;
bool highSetpoint = true;          // start with 100 °C

// Latest data for each unit (timestamp = millis() when the value was parsed)
float         temp1 = NAN;
unsigned long ts1   = 0;
float         temp2 = NAN;
unsigned long ts2   = 0;

void errorBlink() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
}

// Extract the first floating-point number from a line
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

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(1500);
  Serial.println(F("XY-T01 dual-unit assembly test – starting..."));

  // UART0 = XY-T01 #1 (GP0 TX, GP1 RX)
  Serial1.setTX(0);
  Serial1.setRX(1);
  Serial1.begin(9600);
  delay(100);

  // UART1 = XY-T01 #2 (GP8 TX, GP9 RX)
  Serial2.setTX(8);
  Serial2.setRX(9);
  Serial2.begin(9600);
  delay(100);

  // Enable relays and start reporting
  sendCommand(Serial1, "ON");
  sendCommand(Serial2, "ON");
  delay(100);
  sendCommand(Serial1, "start");
  sendCommand(Serial2, "start");
  delay(200);

  // Initial setpoint = 100 °C
  sendCommand(Serial1, "S:100");
  sendCommand(Serial2, "S:100");
  highSetpoint = true;
  lastSetpointMillis = millis();

  Serial.println(F("Both XY-T01 units started."));
  Serial.println(F("Setpoint toggles 100 °C ↔ 0 °C every 3 s."));
  Serial.println(F("Format: ts1_ms,temp1_C,ts2_ms,temp2_C"));
  Serial.println();

  lastPrintMillis = millis();
}

void loop() {
  // ---- Continuously drain both UARTs ----
  while (Serial1.available()) {
    String line = Serial1.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      float t = extractTemperature(line);
      if (!isnan(t)) {
        temp1 = t;
        ts1   = millis();          // timestamp of this reading
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
        ts2   = millis();          // timestamp of this reading
      }
    }
  }

  // ---- Print once per second (matches the ~1 Hz report rate) ----
  if (millis() - lastPrintMillis >= PRINT_INTERVAL_MS) {
    lastPrintMillis = millis();

    // ts1, temp1
    if (ts1 == 0) Serial.print(F("0,nan"));
    else {
      Serial.print(ts1);
      Serial.print(',');
      Serial.print(temp1, 1);
    }
    Serial.print(',');

    // ts2, temp2
    if (ts2 == 0) Serial.print(F("0,nan"));
    else {
      Serial.print(ts2);
      Serial.print(',');
      Serial.print(temp2, 1);
    }
    Serial.println();
  }

  // ---- Toggle setpoint every 3 s ----
  if (millis() - lastSetpointMillis >= SETPOINT_INTERVAL_MS) {
    lastSetpointMillis = millis();
    highSetpoint = !highSetpoint;

    if (highSetpoint) {
      sendCommand(Serial1, "S:100");
      sendCommand(Serial2, "S:100");
    } else {
      sendCommand(Serial1, "S:000");
      sendCommand(Serial2, "S:000");
    }
  }
}