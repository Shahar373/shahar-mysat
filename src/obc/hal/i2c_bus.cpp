#include "hal/i2c_bus.h"
#include "core/log.h"
#include "core/events.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_lock;

void i2c_init() {
  s_lock = xSemaphoreCreateRecursiveMutex();
  Wire.begin(MYSAT_I2C_SDA, MYSAT_I2C_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(50);   // ms; a dead slave must not stall the sensors task
}
void i2c_lock() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void i2c_unlock() { xSemaphoreGiveRecursive(s_lock); }

bool i2c_probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

int i2c_bus_recover() {
  Wire.end();
  pinMode(MYSAT_I2C_SCL, OUTPUT_OPEN_DRAIN);
  pinMode(MYSAT_I2C_SDA, INPUT_PULLUP);
  int pulses = 0;
  for (int i = 0; i < 9 && digitalRead(MYSAT_I2C_SDA) == LOW; i++) {
    digitalWrite(MYSAT_I2C_SCL, LOW); delayMicroseconds(5);
    digitalWrite(MYSAT_I2C_SCL, HIGH); delayMicroseconds(5);
    pulses++;
  }
  // STOP condition
  pinMode(MYSAT_I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(MYSAT_I2C_SDA, LOW); delayMicroseconds(5);
  digitalWrite(MYSAT_I2C_SCL, HIGH); delayMicroseconds(5);
  digitalWrite(MYSAT_I2C_SDA, HIGH); delayMicroseconds(5);
  Wire.begin(MYSAT_I2C_SDA, MYSAT_I2C_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  events_post(EV_I2C_BUS_RECOVERY, pulses, "I2C bus recovery, %d clock pulses", pulses);
  return pulses;
}
