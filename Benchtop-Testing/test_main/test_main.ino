/*
 * Pico Full Production-Style Logger  (Basic Sensing + Heating Test)
 *
 * Duration          : until power loss or manual deactivation
 * Status LED (GP15) : 1 Hz blink for the first 10 s, then OFF
 * XY-T01 heating    : 100 °C (1 s) → 0 °C (1 s) → 20 °C (held for remainder)
 *
 * Sampling (cooperative, non-blocking):
 *   BNO086              100 Hz   (own micros() timestamp + sequence)
 *   MAX31865+MS5611+SHT45  20 Hz   (shared micros() timestamp + sequence)
 *   XY-T01 #1 / #2       1 Hz   (own micros() timestamps)
 *   Note: XY-T01 #1 monitors/controls battery-bank temperature
 *         XY-T01 #2 monitors/controls camera-bay temperature
 *
 * Data handling:
 *   Double buffering – two 24 KB RAM buffers
 *   Flush when 80 % full OR every 2 s (whichever first)
 *   Binary fixed-size records only
 *   Global monotonic uint32_t sequence counter
 *
 *
 * Pins follow the wiring table in pico_wiring.txt (ADA254 on SPI0, etc.).
 * 
 * Use Raspberry Pi Pico/RP2040 by Earle F. Philhower, III
 */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_MAX31865.h>
#include <Adafruit_SHT4x.h>
#include <MS5611.h>

// ----------------------------------------------------------------------
// Pins
// ----------------------------------------------------------------------
const int LED_STATUS = 15;           // external status LED
const int LED_ONBOARD = LED_BUILTIN; // GP25
const int SD_CS = 17;
const int MAX_CS = 20;
const int BNO_RST = 7;
const int MOSFET_PIN = 10;

// ----------------------------------------------------------------------
// Timing constants
// ----------------------------------------------------------------------
const unsigned long ALIVE_BLINK_MS = 10000;    // 10 s
const unsigned long BNO_INTERVAL_US = 10000;   // 100 Hz
const unsigned long SUITE_INTERVAL_US = 50000; // 20 Hz
const unsigned long FLUSH_INTERVAL_MS = 2000;  // max 2 s between flushes

// ----------------------------------------------------------------------
// Double-buffer configuration
// ----------------------------------------------------------------------
const size_t BUFFER_SIZE = 24 * 1024;                    // 24 KB each
const size_t FLUSH_THRESHOLD = (BUFFER_SIZE * 80) / 100; // 80 %

uint8_t bufferA[BUFFER_SIZE];
uint8_t bufferB[BUFFER_SIZE];
uint8_t *activeBuffer = bufferA;
uint8_t *flushingBuffer = nullptr;
size_t activeOffset = 0;
volatile bool flushPending = false;

char logFilename[32];

// ----------------------------------------------------------------------
// Global sequence counter (monotonic)
// ----------------------------------------------------------------------
uint32_t globalSequence = 0;

// ----------------------------------------------------------------------
// Sensor objects
// ----------------------------------------------------------------------
Adafruit_BNO08x bno08x(BNO_RST);
Adafruit_MAX31865 max31865(MAX_CS);
MS5611 ms5611(0x77);
Adafruit_SHT4x sht4;

// ----------------------------------------------------------------------
// Record type identifiers
// ----------------------------------------------------------------------
enum RecordType : uint8_t
{
    REC_BNO086 = 1,
    REC_SUITE = 2,
    REC_XYT01_1 = 3,
    REC_XYT01_2 = 4
};

// ----------------------------------------------------------------------
// Binary record layouts (packed)
// ----------------------------------------------------------------------
#pragma pack(push, 1)

struct BnoRecord
{
    uint8_t type; // REC_BNO086
    uint32_t timestamp_us;
    uint32_t sequence;
    float ax, ay, az;
    float gx, gy, gz;
    float mx, my, mz;
    float qw, qx, qy, qz;
};

struct SuiteRecord
{
    uint8_t type; // REC_SUITE
    uint32_t timestamp_us;
    uint32_t sequence;
    // MAX31865
    float max_temp;
    float max_resistance;
    uint16_t max_raw;
    float max_ratio;
    uint8_t max_fault;
    // MS5611
    float ms_pressure;
    float ms_temp;
    // SHT45
    float sht_temp;
    float sht_humidity;
    uint8_t sht_crc_ok;
};

struct XyRecord
{
    uint8_t type; // REC_XYT01_1 or REC_XYT01_2
    uint32_t timestamp_us;
    uint32_t sequence;
    float temperature;
};

#pragma pack(pop)

// ----------------------------------------------------------------------
// Runtime state
// ----------------------------------------------------------------------
unsigned long lastFlushMs = 0;
unsigned long lastBnoUs = 0;
unsigned long lastSuiteUs = 0;
uint8_t xy1Len = 0;
uint8_t xy2Len = 0;
unsigned long aliveBlinkStartMs = 0;
bool aliveBlinkDone = false;

char xy1Buf[40];
char xy2Buf[40];

// Latest BNO values (filled by the event handler)
float bno_ax = 0, bno_ay = 0, bno_az = 0;
float bno_gx = 0, bno_gy = 0, bno_gz = 0;
float bno_mx = 0, bno_my = 0, bno_mz = 0;
float bno_qw = 0, bno_qx = 0, bno_qy = 0, bno_qz = 0;
bool bnoHasData = false;

// ----------------------------------------------------------------------
// LED helpers
// ----------------------------------------------------------------------
void errorBlink()
{
    while (true)
    {
        digitalWrite(LED_ONBOARD, HIGH);
        digitalWrite(LED_STATUS, HIGH);
        delay(50);
        digitalWrite(LED_ONBOARD, LOW);
        digitalWrite(LED_STATUS, LOW);
        delay(50);
    }
}

// ----------------------------------------------------------------------
// Double-buffer/SD management
// ----------------------------------------------------------------------
size_t flushingSize = 0;

void requestFlush()
{
    if (flushPending || activeOffset == 0)
        return;

    flushingBuffer = activeBuffer;
    flushingSize = activeOffset; // remember how many bytes are valid

    activeBuffer = (activeBuffer == bufferA) ? bufferB : bufferA;
    activeOffset = 0;
    flushPending = true;
}

void writeRecord(const void *data, size_t len)
{
    if (activeOffset + len > BUFFER_SIZE)
    {
        requestFlush();
    }
    memcpy(activeBuffer + activeOffset, data, len);
    activeOffset += len;

    if (activeOffset >= FLUSH_THRESHOLD)
    {
        requestFlush();
    }
}

void serviceFlush()
{
    if (!flushPending)
        return;

    File f = SD.open(logFilename, FILE_WRITE);
    if (f)
    {
        f.write(flushingBuffer, flushingSize);
        f.close();
    }

    endSdTransaction(); // ← critical: force MISO release

    flushPending = false;
    lastFlushMs = millis();
}

// Properly finish any SD transaction and force the card to release MISO
void endSdTransaction()
{
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF); // mandatory dummy byte – forces card to tri-state MISO
                        // Optional but clean:
                        // SPI.endTransaction();
}

// ----------------------------------------------------------------------
// XY-T01 helpers (no line ending)
// ----------------------------------------------------------------------
void sendCommand(HardwareSerial &port, const char *cmd)
{
    port.print(cmd);
    port.flush();
}

float extractTemperature(const String &line)
{
    int i = 0;
    while (i < (int)line.length() &&
           !(isDigit(line[i]) || line[i] == '-' || line[i] == '.'))
        i++;
    if (i >= (int)line.length())
        return NAN;
    String num;
    while (i < (int)line.length() &&
           (isDigit(line[i]) || line[i] == '.' || line[i] == '-'))
    {
        num += line[i++];
    }
    if (num.length() == 0)
        return NAN;
    return num.toFloat();
}

// ----------------------------------------------------------------------
// MAX31865 Helpers
// ----------------------------------------------------------------------
void prepareMax31865Bus()
{
    // Make sure the SD card is deselected
    digitalWrite(SD_CS, HIGH);

    // Re-assert the safe SPI settings (in case an SD operation changed them)
    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));
}

// Stronger recovery sequence
void recoverMax31865Bus()
{
    digitalWrite(SD_CS, HIGH);
    digitalWrite(MAX_CS, HIGH);
    delayMicroseconds(50);

    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));

    // Re-select and re-initialise the critical registers
    max31865.enableBias(true);
    delay(5);
    max31865.clearFault();
    delay(5);
}

// Safe read with automatic retry
bool readMax31865Safe(uint16_t &raw, float &ratio, float &resistance,
                      float &temperature, uint8_t &fault)
{
    const int MAX_ATTEMPTS = 3;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++)
    {
        prepareMax31865Bus(); // existing helper

        raw = max31865.readRTD();
        ratio = raw / 32768.0f;
        resistance = 430.0f * ratio;
        temperature = max31865.calculateTemperature(raw, 100.0f, 430.0f);
        fault = max31865.readFault();
        if (fault)
            max31865.clearFault();

        // Detect the classic garbage pattern
        bool garbage = (raw == 0) || (temperature < -200.0f);

        if (!garbage)
        {
            return true; // good reading
        }

        // Garbage detected – force a stronger recovery and try again
        recoverMax31865Bus();
        delay(10);
    }

    // All attempts failed
    return false;
}

// ----------------------------------------------------------------------
// Sensor initialisation
// ----------------------------------------------------------------------
bool initSensors()
{
    // SPI0 – shared by MAX31865 + ADA254
    pinMode(MAX_CS, OUTPUT);
    digitalWrite(MAX_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    SPI.setRX(16);
    SPI.setTX(19);
    SPI.setSCK(18);
    SPI.begin();

    if (!SD.begin(SD_CS))
    {
        Serial.println(F("SD init failed"));
        return false;
    }
    endSdTransaction(); // clean release after begin
    // Choose the next free numbered filename
    bool found = false;
    for (int i = 0; i < 10000; i++)
    {
        snprintf(logFilename, sizeof(logFilename), "/%04d_flight_log.bin", i);
        if (!SD.exists(logFilename))
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        Serial.println(F("ERROR: all 10000 log filenames are taken"));
        return false;
    }
    Serial.print(F("Logging to "));
    Serial.println(logFilename);

    endSdTransaction(); // ← force clean release after all the exists() calls

    delay(100);

    // Re-lock the SPI clock/mode after all the SD activity
    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));

    prepareMax31865Bus();
    if (!max31865.begin(MAX31865_3WIRE))
    {
        Serial.println(F("MAX31865 init failed"));
        return false;
    }
    max31865.enableBias(true);
    max31865.clearFault();

    // I2C0
    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();
    Wire.setClock(100000);

    if (!bno08x.begin_I2C(0x4B, &Wire))
    {
        Serial.println(F("BNO086 init failed"));
        return false;
    }
    bno08x.enableReport(SH2_ACCELEROMETER, 10000);
    bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000);
    bno08x.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED, 10000);
    bno08x.enableReport(SH2_ROTATION_VECTOR, 10000);

    if (!ms5611.begin())
    {
        Serial.println(F("MS5611 init failed"));
        return false;
    }

    if (!sht4.begin(&Wire))
    {
        Serial.println(F("SHT45 init failed"));
        return false;
    }

    // UARTs for XY-T01
    Serial1.setTX(0);
    Serial1.setRX(1);
    Serial1.begin(9600);
    Serial2.setTX(8);
    Serial2.setRX(9);
    Serial2.begin(9600);
    delay(100);

    sendCommand(Serial1, "ON");
    sendCommand(Serial2, "ON");
    delay(50);
    sendCommand(Serial1, "start");
    sendCommand(Serial2, "start");

    return true;
}

// ----------------------------------------------------------------------
// Startup heating sequence + alive blink
// ----------------------------------------------------------------------
void performStartupSequence()
{
    aliveBlinkStartMs = millis();
    unsigned long phaseStart = millis();

    // 100 °C immediately (held 1 s)
    sendCommand(Serial1, "S:100");
    sendCommand(Serial2, "S:100");

    bool zeroSent = false;
    bool twentySent = false;

    while (millis() - aliveBlinkStartMs < ALIVE_BLINK_MS)
    {
        // 1 Hz blink for first 10 s
        bool on = ((millis() - aliveBlinkStartMs) / 500) % 2 == 0;
        digitalWrite(LED_STATUS, on ? HIGH : LOW);

        unsigned long elapsed = millis() - phaseStart;

        if (elapsed >= 1000 && !zeroSent)
        {
            sendCommand(Serial1, "S:000");
            sendCommand(Serial2, "S:000");
            zeroSent = true;
        }
        if (elapsed >= 2000 && !twentySent)
        {
            sendCommand(Serial1, "S:020");
            sendCommand(Serial2, "S:020");
            twentySent = true;
        }
    }

    digitalWrite(LED_STATUS, LOW);
    aliveBlinkDone = true;
}

// ----------------------------------------------------------------------
// Setup
// ----------------------------------------------------------------------
void setup()
{
    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);
    pinMode(LED_ONBOARD, OUTPUT);
    digitalWrite(LED_ONBOARD, LOW);

    // Power up the rest of the system via the two MOSFETs
    pinMode(MOSFET_PIN, OUTPUT);
    digitalWrite(MOSFET_PIN, HIGH); // <-- ADD THESE TWO LINES

    Serial.begin(115200);
    delay(1000);
    Serial.println(F("Full production-style logger starting..."));

    if (!initSensors())
    {
        errorBlink();
    }
    Serial.println(F("All sensors initialised."));

    performStartupSequence();

    lastFlushMs = millis();
    lastBnoUs = micros();
    lastSuiteUs = micros();
}

// ----------------------------------------------------------------------
// Main cooperative loop
// ----------------------------------------------------------------------
void loop()
{
    unsigned long nowMs = millis();
    unsigned long nowUs = micros();

    // ---- Highest priority: BNO086 100 Hz ----
    if (nowUs - lastBnoUs >= BNO_INTERVAL_US)
    {
        lastBnoUs = nowUs;

        sh2_SensorValue_t event;
        while (bno08x.getSensorEvent(&event))
        {
            switch (event.sensorId)
            {
            case SH2_ACCELEROMETER:
                bno_ax = event.un.accelerometer.x;
                bno_ay = event.un.accelerometer.y;
                bno_az = event.un.accelerometer.z;
                break;
            case SH2_GYROSCOPE_CALIBRATED:
                bno_gx = event.un.gyroscope.x;
                bno_gy = event.un.gyroscope.y;
                bno_gz = event.un.gyroscope.z;
                break;
            case SH2_MAGNETIC_FIELD_CALIBRATED:
                bno_mx = event.un.magneticField.x;
                bno_my = event.un.magneticField.y;
                bno_mz = event.un.magneticField.z;
                break;
            case SH2_ROTATION_VECTOR:
                bno_qw = event.un.rotationVector.real;
                bno_qx = event.un.rotationVector.i;
                bno_qy = event.un.rotationVector.j;
                bno_qz = event.un.rotationVector.k;
                bnoHasData = true;
                break;
            }
        }

        if (bnoHasData)
        {
            BnoRecord rec;
            rec.type = REC_BNO086;
            rec.timestamp_us = nowUs;
            rec.sequence = globalSequence++;
            rec.ax = bno_ax;
            rec.ay = bno_ay;
            rec.az = bno_az;
            rec.gx = bno_gx;
            rec.gy = bno_gy;
            rec.gz = bno_gz;
            rec.mx = bno_mx;
            rec.my = bno_my;
            rec.mz = bno_mz;
            rec.qw = bno_qw;
            rec.qx = bno_qx;
            rec.qy = bno_qy;
            rec.qz = bno_qz;
            writeRecord(&rec, sizeof(rec));
        }
    }

    // ---- 20 Hz atmospheric suite ----
    if (nowUs - lastSuiteUs >= SUITE_INTERVAL_US)
    {
        lastSuiteUs = nowUs;

        SuiteRecord rec;
        rec.type = REC_SUITE;
        rec.timestamp_us = nowUs;
        rec.sequence = globalSequence++;

        // MAX31865
        uint16_t raw;
        float ratio, resistance, temperature;
        uint8_t fault;

        if (readMax31865Safe(raw, ratio, resistance, temperature, fault))
        {
            // use the values
            rec.max_raw = raw;
            rec.max_ratio = ratio;
            rec.max_resistance = resistance;
            rec.max_temp = temperature;
            rec.max_fault = fault;
        }
        else
        {
            // still bad after retries – log a clear failure marker
            rec.max_raw = 0;
            rec.max_ratio = 0;
            rec.max_resistance = 0;
            rec.max_temp = -999.0f; // unmistakable invalid marker
            rec.max_fault = 0xFF;
        }

        // MS5611
        if (ms5611.read() == MS5611_READ_OK)
        {
            rec.ms_temp = ms5611.getTemperature();
            rec.ms_pressure = ms5611.getPressure();
        }
        else
        {
            rec.ms_temp = NAN;
            rec.ms_pressure = NAN;
        }

        // SHT45
        sensors_event_t hum, tem;
        bool ok = sht4.getEvent(&hum, &tem);
        rec.sht_temp = ok ? tem.temperature : NAN;
        rec.sht_humidity = ok ? hum.relative_humidity : NAN;
        rec.sht_crc_ok = ok ? 1 : 0;

        writeRecord(&rec, sizeof(rec));
    }

    // ---- Non-blocking XY-T01 #1 ----
    while (Serial1.available())
    {
        char c = Serial1.read();
        if (c == '\n' || c == '\r')
        {
            if (xy1Len > 0)
            {
                xy1Buf[xy1Len] = '\0';
                float t = extractTemperature(String(xy1Buf));
                if (!isnan(t))
                {
                    XyRecord rec;
                    rec.type = REC_XYT01_1;
                    rec.timestamp_us = nowUs;
                    rec.sequence = globalSequence++;
                    rec.temperature = t;
                    writeRecord(&rec, sizeof(rec));
                }
                xy1Len = 0;
            }
        }
        else if (c >= 32 && c <= 126 && xy1Len < sizeof(xy1Buf) - 1)
        {
            xy1Buf[xy1Len++] = c;
        }
        else
        {
            xy1Len = 0; // overflow or control char – discard
        }
    }

    // ---- Non-blocking XY-T01 #2 ----
    while (Serial2.available())
    {
        char c = Serial2.read();
        if (c == '\n' || c == '\r')
        {
            if (xy2Len > 0)
            {
                xy2Buf[xy2Len] = '\0';
                float t = extractTemperature(String(xy2Buf));
                if (!isnan(t))
                {
                    XyRecord rec;
                    rec.type = REC_XYT01_2;
                    rec.timestamp_us = nowUs;
                    rec.sequence = globalSequence++;
                    rec.temperature = t;
                    writeRecord(&rec, sizeof(rec));
                }
                xy2Len = 0;
            }
        }
        else if (c >= 32 && c <= 126 && xy2Len < sizeof(xy2Buf) - 1)
        {
            xy2Buf[xy2Len++] = c;
        }
        else
        {
            xy2Len = 0;
        }
    }

    // ---- Periodic / threshold flush ----
    if (nowMs - lastFlushMs >= FLUSH_INTERVAL_MS)
    {
        requestFlush();
    }
    serviceFlush();
}
