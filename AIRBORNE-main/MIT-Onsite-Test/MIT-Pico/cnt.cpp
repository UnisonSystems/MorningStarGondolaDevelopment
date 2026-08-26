// cnt.cpp -- CNT scan loop and snapshot publishing

#include "cnt.h"
#include "cnt_data.h"
#include "pinout.h"
#include "sensor_support.h"
#include "settings.h"
#include <SPI.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

static const uint32_t SCAN_INTERVAL_MS = 1000;
static const uint32_t SETTLE_MS = 3;
static const uint32_t AVG_WINDOW_MS = 75;

static uint32_t g_next_scan_ms = 0;

static void publish_cnt_data(const float currents_uA[12]) {
  CNTData pkt;
  memset(&pkt, 0, sizeof(pkt));

  pkt.cnt1.val1 = current_uA_to_scaled(currents_uA[0]);
  pkt.cnt1.val2 = current_uA_to_scaled(currents_uA[1]);
  pkt.cnt1.val3 = current_uA_to_scaled(currents_uA[2]);
  pkt.cnt1.val4 = current_uA_to_scaled(currents_uA[3]);
  pkt.cnt2.val1 = current_uA_to_scaled(currents_uA[4]);
  pkt.cnt2.val2 = current_uA_to_scaled(currents_uA[5]);
  pkt.cnt2.val3 = current_uA_to_scaled(currents_uA[6]);
  pkt.cnt2.val4 = current_uA_to_scaled(currents_uA[7]);
  pkt.cnt3.val1 = current_uA_to_scaled(currents_uA[8]);
  pkt.cnt3.val2 = current_uA_to_scaled(currents_uA[9]);
  pkt.cnt3.val3 = current_uA_to_scaled(currents_uA[10]);
  pkt.cnt3.val4 = current_uA_to_scaled(currents_uA[11]);

  SensorSnapshot snapshot;
  sensors_update(snapshot);
  sensors_apply_to_packet(pkt, snapshot);

  cnt_data_store(pkt);
}

void cnt_begin() {
  pinMode(SPI0_CS_CNT_PIN, OUTPUT);
  digitalWrite(SPI0_CS_CNT_PIN, HIGH);

  SPI.setRX(SPI0_MISO_PIN);
  SPI.setTX(SPI0_MOSI_PIN);
  SPI.setSCK(SPI0_SCK_PIN);
  SPI.begin(false);

  Wire.setSDA(CNT_SDA_PIN);
  Wire.setSCL(CNT_SCL_PIN);
  Wire.begin();
  Wire.setClock(CNT_I2C_FREQ);

  mux_init();
  lmp_init(LOCKWR, TIA_SETTING, REFCN_BIAS_0V5, MODECN_OP_MODE_3LEADAMPC);

  g_next_scan_ms = millis();
}

bool cnt_scan_once() {
  float currents_uA[12];
  memset(currents_uA, 0, sizeof(currents_uA));

  for (int dev = 0; dev < N_DEVICES; ++dev) {
    mux_select((uint8_t)dev);
    delay(SETTLE_MS);

    int32_t acc = 0;
    int n = 0;
    uint32_t start_ms = millis();
    while ((millis() - start_ms) < AVG_WINDOW_MS) {
      acc += (int32_t)readadc();
      ++n;
      delayMicroseconds(100);
    }

    float raw_mean = (n > 0) ? ((float)acc / (float)n) : 0.0f;
    float current_uA = raw_to_current(raw_mean);
    float r_ohm = device_ohms(current_uA, dev);
    if (!isnan(r_ohm) && r_ohm > 0.0f) {
      currents_uA[dev] = V_REPORT / r_ohm * 1e6f;
    } else {
      currents_uA[dev] = current_uA;
    }
  }

  publish_cnt_data(currents_uA);
  return true;
}

void cnt_loop() {
  uint32_t now = millis();
  if ((int32_t)(now - g_next_scan_ms) < 0) {
    return;
  }
  if (cnt_scan_once()) {
    g_next_scan_ms = now + SCAN_INTERVAL_MS;
  } else {
    g_next_scan_ms = now + 100;
  }
}