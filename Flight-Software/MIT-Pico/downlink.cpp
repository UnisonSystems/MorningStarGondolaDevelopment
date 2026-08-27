#include <Arduino.h>
#include "downlink.h"

#include <Wire.h>
#include <string.h>
#include <math.h>
#include "pinout.h"

static DownlinkSnapshot g_latest;
static uint8_t g_blob[sizeof(DownlinkSnapshot)];

static void storeBlob()
{
    noInterrupts();
    memcpy(g_blob, &g_latest, sizeof(g_blob));
    interrupts();
}

static void loadBlob(uint8_t *dst)
{
    noInterrupts();
    memcpy(dst, g_blob, sizeof(g_blob));
    interrupts();
}

static int16_t encodeTempC(float t)
{
    return (int16_t)round(t * 100.0);
}

static uint16_t encodeRh(float rh)
{
    return (uint16_t)round(rh * 100.0);
}

void onRequest()
{
    uint8_t buf[sizeof(DownlinkSnapshot)];
    loadBlob(buf);
    Wire1.write(buf, sizeof(buf));
}

void initDownlink()
{
    g_latest.t_max31865 = DL_TEMP_SENTINEL;
    g_latest.t_sht45 = DL_TEMP_SENTINEL;
    g_latest.t_ms5611 = DL_TEMP_SENTINEL;
    g_latest.t_xyt011 = DL_TEMP_SENTINEL;
    g_latest.t_xyt012 = DL_TEMP_SENTINEL;
    g_latest.h_sht45 = DL_HUM_SENTINEL;
    g_latest.p_ms5611 = NAN;
    storeBlob();

    Wire1.setSDA(I2C1_SDA_PIN);
    Wire1.setSCL(I2C1_SCL_PIN);
    Wire1.begin(TINY4FSK_SLAVE_ADDR);
    Wire1.setClock(I2C1_FREQ);
    Wire1.onRequest(onRequest);
}

void downlinkUpdateSuite(bool maxOk, float maxTemp,
                         bool msOk, float msTemp, float msPressure,
                         bool shtOk, float shtTemp, float shtHumidity)
{
    g_latest.t_max31865 = maxOk ? encodeTempC(maxTemp) : DL_TEMP_SENTINEL;
    g_latest.t_sht45 = shtOk ? encodeTempC(shtTemp) : DL_TEMP_SENTINEL;
    g_latest.t_ms5611 = msOk ? encodeTempC(msTemp) : DL_TEMP_SENTINEL;
    g_latest.h_sht45 = shtOk ? encodeRh(shtHumidity) : DL_HUM_SENTINEL;
    g_latest.p_ms5611 = msOk ? msPressure : NAN;
    storeBlob();
}

void downlinkUpdateXy1(float t)
{
    g_latest.t_xyt011 = encodeTempC(t);
    storeBlob();
}

void downlinkUpdateXy2(float t)
{
    g_latest.t_xyt012 = encodeTempC(t);
    storeBlob();
}
