#ifndef I2C_INIT_H
#define I2C_INIT_H

#include <Arduino.h>

void i2c_slave_begin();
void i2c_slave_pause_for_master();
void i2c_slave_resume();
bool i2c_slave_active();

#endif