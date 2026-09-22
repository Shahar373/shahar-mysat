// OBC side of the AUX (Arduino Nano) I2C protocol. Caller does not need to hold the I2C lock.
#pragma once
#include <Arduino.h>
#include "mysat_icd.h"

// Sends a command in whichever protocol the AUX on the bus understands. A v2 AUX (src/aux/) gets
// the framed, CRC-protected form; the stock kit firmware gets the single-byte v1 form when one
// exists (wings open / close) and nothing at all otherwise -- see aux_legacy_equivalent() in the
// ICD for why a frame must never reach it. Returns false when nothing was sent or acknowledged.
bool aux_send(uint8_t cmd, uint8_t arg);
bool aux_send_legacy(uint8_t cmd);              // 1-byte v1 command, unconditionally
bool aux_read_status(AuxStatus& out);           // false if no answer / bad crc (v1 has no readback)
bool aux_present();                             // last status read succeeded
void        aux_probe();                        // one status read; settles which firmware answers
uint8_t     aux_protocol();                     // AuxProto: what the last probe found
const char* aux_protocol_str();
bool        aux_has_readback();                 // true only for a v2 AUX: angle confirmation exists
// True only when the AUX answers and reports a sweep in progress. An AUX that cannot be read
// (stock firmware has no status readback) counts as idle, so commands to it still go out.
bool aux_servo_busy();
void aux_heartbeat(uint8_t mode);
