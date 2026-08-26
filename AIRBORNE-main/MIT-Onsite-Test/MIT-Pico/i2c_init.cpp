#include "i2c_init.h"
#include "pinout.h"
#include "tiny4fsk.h"
#include <Wire.h>

static bool g_active = false;
static bool g_paused = false;

void i2c_slave_begin() {
	Wire1.end();
	Wire1.setSDA(I2C1_SDA_PIN);
	Wire1.setSCL(I2C1_SCL_PIN);
	Wire1.begin(TINY4FSK_SLAVE_ADDR);
	Wire1.onRequest(requestEvent);
	g_active = true;
	g_paused = false;
}

void i2c_slave_pause_for_master() {
	if (!g_active || g_paused) {
		return;
	}
	Wire1.end();
	Wire1.setSDA(I2C1_SDA_PIN);
	Wire1.setSCL(I2C1_SCL_PIN);
	Wire1.begin();
	Wire1.setClock(I2C1_FREQ);
	g_paused = true;
}

void i2c_slave_resume() {
	if (!g_active || !g_paused) {
		return;
	}
	Wire1.end();
	Wire1.setSDA(I2C1_SDA_PIN);
	Wire1.setSCL(I2C1_SCL_PIN);
	Wire1.begin(TINY4FSK_SLAVE_ADDR);
	Wire1.onRequest(requestEvent);
	g_paused = false;
}

bool i2c_slave_active() {
	return g_active && !g_paused;
}