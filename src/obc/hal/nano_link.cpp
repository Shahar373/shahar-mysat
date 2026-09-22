#include "hal/nano_link.h"
#include "hal/i2c_bus.h"
#include "core/fdir.h"
#include "crc.h"

#include "core/log.h"

static bool    s_present = false;
static uint8_t s_proto = AUX_PROTO_UNKNOWN;

// The kit ships with a Nano firmware that has no status readback, so "does a status read answer
// with a valid frame" is exactly the question "is this the v2 firmware". A legacy verdict is never
// final: every later successful read upgrades it, so a Nano that boots a little after the ESP32
// is still recognised, and once v2 has been seen a transient bus error does not demote it (the v2
// firmware accepts single-byte commands too, so nothing would break, but the heartbeat and the
// readback would silently stop being used).
static void probe() {
  AuxStatus st;
  if (aux_read_status(st)) return;                 // aux_read_status latches v2 itself
  if (s_proto == AUX_PROTO_UNKNOWN) {
    s_proto = AUX_PROTO_LEGACY;
    LOGW("AUX", "no status readback: assuming the stock Nano firmware (wings open/close only, no confirmation)");
  }
}

void aux_probe() { probe(); }
uint8_t aux_protocol() { return s_proto; }
bool aux_has_readback() { return s_proto == AUX_PROTO_V2; }
const char* aux_protocol_str() {
  switch (s_proto) {
    case AUX_PROTO_V2: return "v2";
    case AUX_PROTO_LEGACY: return "stock (v1, no readback)";
    default: return "not probed";
  }
}

static bool send_framed(uint8_t cmd, uint8_t arg) {
  uint8_t f[AUX_FRAME_LEN] = { AUX_FRAME_MAGIC, cmd, arg, 0 };
  f[3] = crc8_smbus(f, 3);
  I2cGuard g;
  Wire.beginTransmission(MYSAT_AUX_I2C_ADDR);
  Wire.write(f, AUX_FRAME_LEN);
  bool ok = Wire.endTransmission() == 0;
  if (ok) fdir_dev_ok(DEV_AUX); else fdir_dev_error(DEV_AUX);
  return ok;
}

bool aux_send(uint8_t cmd, uint8_t arg) {
  if (s_proto == AUX_PROTO_UNKNOWN) probe();
  if (s_proto == AUX_PROTO_V2) return send_framed(cmd, arg);
  int legacy = aux_legacy_equivalent(cmd, arg);
  if (legacy < 0) {
    LOGD("AUX", "command 0x%02X has no v1 equivalent, not sent to the stock firmware", cmd);
    return false;
  }
  bool ok = aux_send_legacy((uint8_t)legacy);
  if (ok) fdir_dev_ok(DEV_AUX); else fdir_dev_error(DEV_AUX);
  return ok;
}

bool aux_send_legacy(uint8_t cmd) {
  I2cGuard g;
  Wire.beginTransmission(MYSAT_AUX_I2C_ADDR);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

bool aux_read_status(AuxStatus& out) {
  I2cGuard g;
  uint8_t n = Wire.requestFrom((uint8_t)MYSAT_AUX_I2C_ADDR, (uint8_t)AUX_STATUS_LEN);
  if (n != AUX_STATUS_LEN) { while (Wire.available()) Wire.read(); s_present = false; return false; }
  uint8_t buf[AUX_STATUS_LEN];
  for (uint8_t i = 0; i < AUX_STATUS_LEN; i++) buf[i] = Wire.read();
  if (buf[0] != AUX_STATUS_MAGIC || crc8_smbus(buf, AUX_STATUS_LEN - 1) != buf[AUX_STATUS_LEN - 1]) { s_present = false; return false; }
  memcpy(&out, buf, AUX_STATUS_LEN);
  s_present = true;
  if (s_proto != AUX_PROTO_V2) { s_proto = AUX_PROTO_V2; LOGI("AUX", "v2 firmware detected (fw %u.%u), status readback available", out.fw >> 4, out.fw & 0x0F); }
  return true;
}

bool aux_present() { return s_present; }
bool aux_servo_busy() { AuxStatus st; return aux_read_status(st) && st.servo_state != AUX_SERVO_OFF; }
void aux_heartbeat(uint8_t mode) { aux_send(AUX_CMD_HEARTBEAT, mode); }
