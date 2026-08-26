#include "cnt.h"
#include "i2c_init.h"
#include "sensor_support.h"
#include "tiny4fsk.h"

CNTData currents_test;

void setup() {
    Serial.begin(115200);
    sensors_begin();
    cnt_begin();
    i2c_slave_begin();
}

void loop() {
    //cnt_loop();
    //delayMicroseconds(100);
    cnt_scan_once();
    Serial.print("Sensor Data: ");
    SensorSnapshot snapshot;
    sensors_update(snapshot);
    Serial.print("SHT45: ");
    Serial.print(snapshot.sht45_temperature_c);
    Serial.print(" C, ");
    Serial.print(snapshot.sht45_humidity_pct);
    Serial.print(" %, MS5611: ");
    Serial.print(snapshot.ms5611_temperature_c);
    Serial.print(" C, ");
    Serial.print(snapshot.ms5611_pressure_hpa);
    Serial.print(" hPa, ICM20948: ");
    Serial.print(snapshot.icm2098_accel_x_g);
    Serial.print(" g, ");
    Serial.print(snapshot.icm2098_accel_y_g);
    Serial.print(" g, ");
    Serial.print(snapshot.icm2098_accel_z_g);
    Serial.print(" g, ");
    Serial.print(snapshot.icm2098_gyro_x_dps);
    Serial.print(" dps, ");
    Serial.print(snapshot.icm2098_gyro_y_dps);
    Serial.print(" dps, ");
    Serial.print(snapshot.icm2098_gyro_z_dps);
    Serial.print(" dps, MAX31865: ");
    Serial.print(snapshot.max31865_temperature_c);
    Serial.print(" C, XY-T01 Status: ");
    Serial.print(snapshot.xyt01_status);
    Serial.print(", Sensor Status Bits: ");
    Serial.println(snapshot.sensor_status_bits, BIN); 
    // cnt_data_load(currents_test);
    // Serial.print("CNT1 Values: ");
    // Serial.print(currents_test.cnt1.val1);
    // Serial.print(',');
    // Serial.print(currents_test.cnt1.val2);
    // Serial.print(',');
    // Serial.print(currents_test.cnt1.val3);
    // Serial.print(',');
    // Serial.print(currents_test.cnt1.val4);
    // Serial.print(", CNT2 Values: ");
    // Serial.print(currents_test.cnt2.val1);
    // Serial.print(',');
    // Serial.print(currents_test.cnt2.val2);
    // Serial.print(',');
    // Serial.print(currents_test.cnt2.val3);
    // Serial.print(',');
    // Serial.print(currents_test.cnt2.val4);
    // Serial.print(", CNT3 Values: ");
    // Serial.print(currents_test.cnt3.val1);
    // Serial.print(',');
    // Serial.print(currents_test.cnt3.val2);
    // Serial.print(',');
    // Serial.print(currents_test.cnt3.val3);
    // Serial.print(',');
    // Serial.print(currents_test.cnt3.val4);
    // Serial.println();
}