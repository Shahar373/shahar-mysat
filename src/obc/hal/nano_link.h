// OBC side of the AUX (Arduino Nano) I2C protocol. Caller does not need to hold the I2C lock.
#pragma once
#include <Arduino.h>
#include "mysat_icd.h"

bool aux_send(uint8_t cmd, uint8_t arg);        // framed v2 command with crc8
bool aux_send_legacy(uint8_t cmd);              // 1-byte v1 command (for old Nano firmware)
bool aux_read_status(AuxStatus& out);           // false if no answer / bad crc (old firmware has no readback)
bool aux_present();                             // last status read succeeded
void aux_heartbeat(uint8_t mode);
