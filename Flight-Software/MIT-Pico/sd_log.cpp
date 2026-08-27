#include <Arduino.h>
#include "sd_log.h"

#include <SPI.h>
#include <SD.h>
#include "pinout.h"

uint32_t globalSequence = 0;
unsigned long lastFlushMs = 0;

static uint8_t bufferA[BUFFER_SIZE];
static uint8_t bufferB[BUFFER_SIZE];
static uint8_t *activeBuffer = bufferA;
static uint8_t *flushingBuffer = nullptr;
static size_t activeOffset = 0;
static volatile bool flushPending = false;
static size_t flushingSize = 0;
static char logFilename[LOG_FILENAME_SIZE];

void endSdTransaction()
{
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);
}

void requestFlush()
{
    if (flushPending || activeOffset == 0)
        return;

    flushingBuffer = activeBuffer;
    flushingSize = activeOffset;

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

    endSdTransaction();

    flushPending = false;
    lastFlushMs = millis();
}

bool initLog()
{
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    SPI.setRX(SPI0_RX_PIN);
    SPI.setTX(SPI0_TX_PIN);
    SPI.setSCK(SPI0_SCK_PIN);
    SPI.begin();

    if (!SD.begin(SD_CS))
    {
        Serial.println(F("SD init failed"));
        return false;
    }
    endSdTransaction();

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

    endSdTransaction();

    return true;
}
