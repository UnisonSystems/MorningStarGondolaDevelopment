#include <Arduino.h>
#include "heaters.h"

#include "pinout.h"
#include "records.h"
#include "sd_log.h"
#include "downlink.h"

static uint8_t xy1Len = 0;
static uint8_t xy2Len = 0;
static char xy1Buf[XY_LINE_BUF_SIZE];
static char xy2Buf[XY_LINE_BUF_SIZE];

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

void initXy()
{
    Serial1.setTX(XY1_TX_PIN);
    Serial1.setRX(XY1_RX_PIN);
    Serial1.begin(XY_BAUD);
    Serial2.setTX(XY2_TX_PIN);
    Serial2.setRX(XY2_RX_PIN);
    Serial2.begin(XY_BAUD);
    delay(100);

    sendCommand(Serial1, "ON");
    sendCommand(Serial2, "ON");
    delay(50);
    sendCommand(Serial1, "start");
    sendCommand(Serial2, "start");
}

void performStartupSequence()
{
    unsigned long aliveBlinkStartMs = millis();
    unsigned long phaseStart = millis();

    sendCommand(Serial1, "S:100");
    sendCommand(Serial2, "S:100");

    bool zeroSent = false;
    bool twentySent = false;

    while (millis() - aliveBlinkStartMs < ALIVE_BLINK_MS)
    {
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
}

void pollXy(unsigned long nowUs)
{
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
                    downlinkUpdateXy1(t);
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
            xy1Len = 0;
        }
    }

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
                    downlinkUpdateXy2(t);
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
}
