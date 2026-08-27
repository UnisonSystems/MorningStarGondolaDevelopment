#include <Arduino.h>
#include "sensors.h"

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_MAX31865.h>
#include <Adafruit_SHT4x.h>
#include <MS5611.h>
#include "pinout.h"
#include "records.h"
#include "sd_log.h"
#include "heaters.h"
#include "downlink.h"

unsigned long lastBnoUs = 0;
unsigned long lastSuiteUs = 0;

static Adafruit_BNO08x bno08x(BNO_RST);
static Adafruit_MAX31865 max31865(MAX_CS);
static MS5611 ms5611(MS5611_ADDR);
static Adafruit_SHT4x sht4;

static float bno_ax = 0, bno_ay = 0, bno_az = 0;
static float bno_gx = 0, bno_gy = 0, bno_gz = 0;
static float bno_mx = 0, bno_my = 0, bno_mz = 0;
static float bno_qw = 0, bno_qx = 0, bno_qy = 0, bno_qz = 0;
static bool bnoHasData = false;

void prepareMax31865Bus()
{
    digitalWrite(SD_CS, HIGH);
    SPI.beginTransaction(SPISettings(MAX_SPI_HZ, MSBFIRST, SPI_MODE1));
}

void recoverMax31865Bus()
{
    digitalWrite(SD_CS, HIGH);
    digitalWrite(MAX_CS, HIGH);
    delayMicroseconds(50);

    SPI.beginTransaction(SPISettings(MAX_SPI_HZ, MSBFIRST, SPI_MODE1));

    max31865.enableBias(true);
    delay(5);
    max31865.clearFault();
    delay(5);
}

bool readMax31865Safe(uint16_t &raw, float &ratio, float &resistance,
                      float &temperature, uint8_t &fault)
{
    const int MAX_ATTEMPTS = 3;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++)
    {
        prepareMax31865Bus();

        raw = max31865.readRTD();
        ratio = raw / 32768.0f;
        resistance = MAX_RREF * ratio;
        temperature = max31865.calculateTemperature(raw, MAX_RNOMINAL, MAX_RREF);
        fault = max31865.readFault();
        if (fault)
            max31865.clearFault();

        bool garbage = (raw == 0) || (temperature < -200.0f);

        if (!garbage)
        {
            return true;
        }

        recoverMax31865Bus();
        delay(10);
    }

    return false;
}

bool initSensors()
{
    pinMode(MAX_CS, OUTPUT);
    digitalWrite(MAX_CS, HIGH);

    if (!initLog())
        return false;

    delay(100);

    SPI.beginTransaction(SPISettings(MAX_SPI_HZ, MSBFIRST, SPI_MODE1));

    prepareMax31865Bus();
    if (!max31865.begin(MAX31865_3WIRE))
    {
        Serial.println(F("MAX31865 init failed"));
        return false;
    }
    max31865.enableBias(true);
    max31865.clearFault();

    Wire.setSDA(I2C0_SDA_PIN);
    Wire.setSCL(I2C0_SCL_PIN);
    Wire.begin();
    Wire.setClock(I2C0_FREQ);

    if (!bno08x.begin_I2C(BNO086_ADDR, &Wire))
    {
        Serial.println(F("BNO086 init failed"));
        return false;
    }
    bno08x.enableReport(SH2_ACCELEROMETER, BNO_INTERVAL_US);
    bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, BNO_INTERVAL_US);
    bno08x.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED, BNO_INTERVAL_US);
    bno08x.enableReport(SH2_ROTATION_VECTOR, BNO_INTERVAL_US);

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

    initXy();

    return true;
}

void pollBno(unsigned long nowUs)
{
    if (nowUs - lastBnoUs < BNO_INTERVAL_US)
        return;

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

void pollSuite(unsigned long nowUs)
{
    if (nowUs - lastSuiteUs < SUITE_INTERVAL_US)
        return;

    lastSuiteUs = nowUs;

    SuiteRecord rec;
    rec.type = REC_SUITE;
    rec.timestamp_us = nowUs;
    rec.sequence = globalSequence++;

    uint16_t raw;
    float ratio, resistance, temperature;
    uint8_t fault;
    bool maxOk = readMax31865Safe(raw, ratio, resistance, temperature, fault);

    if (maxOk)
    {
        rec.max_raw = raw;
        rec.max_ratio = ratio;
        rec.max_resistance = resistance;
        rec.max_temp = temperature;
        rec.max_fault = fault;
    }
    else
    {
        rec.max_raw = 0;
        rec.max_ratio = 0;
        rec.max_resistance = 0;
        rec.max_temp = -999.0f;
        rec.max_fault = 0xFF;
    }

    bool msOk = (ms5611.read() == MS5611_READ_OK);
    if (msOk)
    {
        rec.ms_temp = ms5611.getTemperature();
        rec.ms_pressure = ms5611.getPressure();
    }
    else
    {
        rec.ms_temp = NAN;
        rec.ms_pressure = NAN;
    }

    sensors_event_t hum, tem;
    bool shtOk = sht4.getEvent(&hum, &tem);
    rec.sht_temp = shtOk ? tem.temperature : NAN;
    rec.sht_humidity = shtOk ? hum.relative_humidity : NAN;
    rec.sht_crc_ok = shtOk ? 1 : 0;

    writeRecord(&rec, sizeof(rec));
    downlinkUpdateSuite(maxOk, rec.max_temp, msOk, rec.ms_temp, rec.ms_pressure,
                        shtOk, rec.sht_temp, rec.sht_humidity);
}
