// Single I2C bus (SDA 15 / SCL 13) shared by every sensor, the RTC and the AUX Nano.
// The sensors task is the main user; anyone else must hold the lock around Wire calls.
#pragma once
#include <Arduino.h>
#include <Wire.h>

#define MYSAT_I2C_SDA 15
#define MYSAT_I2C_SCL 13

void i2c_init();
void i2c_lock();
void i2c_unlock();
bool i2c_probe(uint8_t addr);          // caller holds the lock
int  i2c_bus_recover();                // caller holds the lock; toggles SCL 9x to free a stuck slave
struct I2cGuard { I2cGuard() { i2c_lock(); } ~I2cGuard() { i2c_unlock(); } };
