#include "pinout.h"
#include "sd_log.h"
#include "sensors.h"
#include "heaters.h"
#include "downlink.h"

void setup()
{
    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);
    pinMode(LED_ONBOARD, OUTPUT);
    digitalWrite(LED_ONBOARD, LOW);

    pinMode(MOSFET_PIN, OUTPUT);
    digitalWrite(MOSFET_PIN, HIGH);

    Serial.begin(USB_SERIAL_BAUD);
    delay(1000);
    Serial.println(F("Full production-style logger starting..."));

    if (!initSensors())
    {
        errorBlink();
    }

    initDownlink();
    performStartupSequence();

    lastFlushMs = millis();
    lastBnoUs = micros();
    lastSuiteUs = micros();
}

// Double-buffer invariant (do not break without a bounds check in writeRecord):
//   requestFlush() swaps to an empty buffer and sets flushPending.
//   serviceFlush() must run on the same core, in this loop(), before that
//   new buffer can fill. owc is blocking, so writeRecord is not called
//   during the SD transfer. flushPending is cleared when owc returns.
// Original code is safe because sampling cannot outrun a pending flush.
// Unsafe if flush becomes asynchronous, if records are written from an
// ISR / onRequest, or if serviceFlush() is removed from the bottom of
// loop(). In any of those cases writeRecord() must refuse the memcpy
// when requestFlush() cannot swap, or it will write off the end of the
// active 24 KB buffer.
void loop()
{
    unsigned long nowMs = millis();
    unsigned long nowUs = micros();

    pollBno(nowUs);
    pollSuite(nowUs);
    pollXy(nowUs);

    if (nowMs - lastFlushMs >= FLUSH_INTERVAL_MS)
    {
        requestFlush();
    }
    serviceFlush();
}
