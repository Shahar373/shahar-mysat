#include "hal/nano_link.h"
#include "hal/i2c_bus.h"
#include "core/fdir.h"
#include "crc.h"

static bool s_present = false;

bool aux_send(uint8_t cmd, uint8_t arg) {
  uint8_t f[AUX_FRAME_LEN] = { AUX_FRAME_MAGIC, cmd, arg, 0 };
  f[3] = crc8_smbus(f, 3);
  I2cGuard g;
  Wire.beginTransmission(MYSAT_AUX_I2C_ADDR);
  Wire.write(f, AUX_FRAME_LEN);
  bool ok = Wire.endTransmission() == 0;
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
  return true;
}

bool aux_present() { return s_present; }
void aux_heartbeat(uint8_t mode) { aux_send(AUX_CMD_HEARTBEAT, mode); }
