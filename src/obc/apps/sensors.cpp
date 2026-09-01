#include "apps/sensors.h"
#include "core/bus.h"
#include "core/fdir.h"
#include "core/log.h"
#include "core/events.h"
#include "core/mission_clock.h"
#include "core/params_store.h"
#include "hal/i2c_bus.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <RtcDS3231.h>
#include <bsec.h>
#include <Preferences.h>
#include <math.h>

// ---------------------------------------------------------------- BME680 / BSEC (environment)
#define BME680_I2C_ADDR 0x77
static Bsec s_iaq;
static bool s_bme_present = false;
static uint32_t s_bsec_last_save_ms = 0;
static const uint32_t BSEC_SAVE_INTERVAL_MS = 3600000UL;

static void bsec_load_state() {
  Preferences prefs;
  if (!prefs.begin("bsec", true)) return;
  if (prefs.isKey("state")) {
    uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
    size_t n = prefs.getBytes("state", state, sizeof state);
    if (n == BSEC_MAX_STATE_BLOB_SIZE) s_iaq.setState(state);
  }
  prefs.end();
}
static void bsec_save_state() {
  uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
  s_iaq.getState(state);
  Preferences prefs;
  if (!prefs.begin("bsec", false)) return;
  prefs.putBytes("state", state, sizeof state);
  prefs.end();
}

static bool bme_init() {
  if (!i2c_probe(BME680_I2C_ADDR)) return false;
  s_iaq.begin(BME680_I2C_ADDR, Wire);
  if (s_iaq.bsecStatus != BSEC_OK) return false;
  bsec_virtual_sensor_t sensors[] = { BSEC_OUTPUT_IAQ, BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY, BSEC_OUTPUT_RAW_PRESSURE, BSEC_OUTPUT_RAW_GAS };
  s_iaq.updateSubscription(sensors, 5, BSEC_SAMPLE_RATE_LP);   // LP: one measurement / 3 s, gentler on the heater
  bsec_load_state();
  return true;
}

static void bme_read(EnvData& out) {
  I2cGuard g;
  if (s_iaq.run()) {
    out.valid = true;
    out.temperature_c = s_iaq.temperature;
    out.humidity_pct = s_iaq.humidity;
    out.pressure_hpa = s_iaq.pressure / 100.0f;
    out.gas_kohm = s_iaq.gasResistance / 1000.0f;
    out.iaq = s_iaq.iaq;
    out.iaq_accuracy = s_iaq.iaqAccuracy;
    fdir_dev_ok(DEV_BME680);
  }
  if (millis() - s_bsec_last_save_ms > BSEC_SAVE_INTERVAL_MS) { bsec_save_state(); s_bsec_last_save_ms = millis(); }
}

// ---------------------------------------------------------------- MPU6500 / MPU9250 (attitude)
#define MPU_ADDR 0x69
#define REG_WHOAMI 0x75
#define REG_PWR_MGMT_1 0x6B
#define REG_ACCEL_XOUT_H 0x3B
#define REG_GYRO_CONFIG 0x1B

static bool s_imu_present = false;
static uint8_t s_imu_whoami = 0;
static float s_gyro_lsb_per_dps = 32.8f;   // +-1000 dps range (stock firmware used +-250, which
                                            // saturates when a person spins the cube by hand)
static int16_t s_gyro_bias[3] = {0, 0, 0};
static bool s_calibrated = false;
static bool s_cal_requested = false;
static float s_angle[3] = {0, 0, 0};       // roll(x), pitch(y), yaw(z), degrees
static float s_offset[3] = {0, 0, 0};
static uint32_t s_imu_samples = 0;
static uint32_t s_imu_last_us = 0;
static float s_rate_dps[3] = {0, 0, 0};
static float s_accel_g[3] = {0, 0, 0};
static float s_imu_temp_c = 0;

static void imu_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
static bool imu_burst_read(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t got = Wire.requestFrom((uint8_t)MPU_ADDR, len);
  if (got != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}
static int16_t raw16(const uint8_t* b, uint8_t i) { return (int16_t)((b[i] << 8) | b[i + 1]); }

static bool imu_init() {
  I2cGuard g;
  if (!i2c_probe(MPU_ADDR)) return false;
  Wire.beginTransmission(MPU_ADDR); Wire.write(REG_WHOAMI);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1) != 1) return false;
  s_imu_whoami = Wire.read();
  imu_write(REG_PWR_MGMT_1, 0x80); delay(100);
  imu_write(REG_PWR_MGMT_1, 0x00); delay(10);
  imu_write(0x1A, 3);                 // DLPF ~41 Hz
  imu_write(0x19, 4);                 // sample rate divider -> 200 Hz base rate
  imu_write(REG_GYRO_CONFIG, 0x08);   // +-1000 dps (was 0x00 / +-250 dps in the stock firmware)
  imu_write(0x1C, 0x00);              // +-2 g accel
  s_gyro_bias[0] = g_params.gyro_bias[0]; s_gyro_bias[1] = g_params.gyro_bias[1]; s_gyro_bias[2] = g_params.gyro_bias[2];
  s_offset[0] = g_params.att_offset[0]; s_offset[1] = g_params.att_offset[1]; s_offset[2] = g_params.att_offset[2];
  s_calibrated = g_params.imu_calibrated;
  s_imu_last_us = micros();
  return true;
}

void sensors_request_calibration() { s_cal_requested = true; }
bool sensors_calibration_pending() { return s_cal_requested; }

// One 200 Hz sample: proper dt integration (this is the fix for the stock firmware's 80%-lost-rotation bug).
static void imu_sample() {
  uint8_t buf[14];
  if (!imu_burst_read(REG_ACCEL_XOUT_H, buf, 14)) { fdir_dev_error(DEV_IMU); return; }
  fdir_dev_ok(DEV_IMU);

  int16_t ax = raw16(buf, 0), ay = raw16(buf, 2), az = raw16(buf, 4);
  int16_t traw = raw16(buf, 6);
  int16_t gx = raw16(buf, 8) - s_gyro_bias[0], gy = raw16(buf, 10) - s_gyro_bias[1], gz = raw16(buf, 12) - s_gyro_bias[2];

  s_accel_g[0] = ax / 16384.0f; s_accel_g[1] = ay / 16384.0f; s_accel_g[2] = az / 16384.0f;
  s_imu_temp_c = traw / 333.87f + 21.0f;
  s_rate_dps[0] = gx / s_gyro_lsb_per_dps; s_rate_dps[1] = gy / s_gyro_lsb_per_dps; s_rate_dps[2] = gz / s_gyro_lsb_per_dps;

  uint32_t now = micros();
  float dt = (now - s_imu_last_us) / 1000000.0f;
  s_imu_last_us = now;
  if (dt <= 0 || dt > 0.05f) dt = 0.005f;   // 200 Hz nominal; guard the very first sample only

  s_angle[0] += s_rate_dps[0] * dt;
  s_angle[1] += s_rate_dps[1] * dt;
  s_angle[2] += s_rate_dps[2] * dt;
  if (s_angle[2] > 180) s_angle[2] -= 360; if (s_angle[2] < -180) s_angle[2] += 360;

  // gentle complementary correction on roll/pitch from gravity, keeps long-run drift bounded
  float accel_roll  = atan2f(s_accel_g[1], s_accel_g[2]) * 180.0f / (float)M_PI;
  float accel_pitch = atan2f(-s_accel_g[0], sqrtf(s_accel_g[1]*s_accel_g[1] + s_accel_g[2]*s_accel_g[2])) * 180.0f / (float)M_PI;
  s_angle[0] = 0.98f * s_angle[0] + 0.02f * accel_roll;
  s_angle[1] = 0.98f * s_angle[1] + 0.02f * accel_pitch;

  s_imu_samples++;
}

static void imu_maybe_calibrate() {
  if (!s_cal_requested) return;
  LOGI("IMU", "gyro calibration: keep still for 3 s");
  int32_t sum[3] = {0, 0, 0}; int n = 0;
  uint32_t start = millis();
  while (millis() - start < 3000) {
    uint8_t buf[14];
    if (imu_burst_read(REG_ACCEL_XOUT_H, buf, 14)) {
      sum[0] += raw16(buf, 8); sum[1] += raw16(buf, 10); sum[2] += raw16(buf, 12);
      n++;
    }
    delay(2);
  }
  if (n > 100) {
    s_gyro_bias[0] = sum[0] / n; s_gyro_bias[1] = sum[1] / n; s_gyro_bias[2] = sum[2] / n;
    s_angle[0] = s_angle[1] = s_angle[2] = 0;
    s_offset[0] = s_offset[1] = s_offset[2] = 0;
    s_calibrated = true;
    params_lock();
    g_params.gyro_bias[0] = s_gyro_bias[0]; g_params.gyro_bias[1] = s_gyro_bias[1]; g_params.gyro_bias[2] = s_gyro_bias[2];
    g_params.att_offset[0] = g_params.att_offset[1] = g_params.att_offset[2] = 0;
    g_params.imu_calibrated = 1;
    params_unlock();
    params_save();
    LOGI("IMU", "calibration complete (%d samples)", n);
  } else {
    LOGW("IMU", "calibration failed: too few samples");
  }
  s_cal_requested = false;
}

void sensors_request_gravity_align() {
  // zero the current attitude reading -- lets the operator define "level" without a full recalibration
  s_offset[0] = s_angle[0]; s_offset[1] = s_angle[1]; s_offset[2] = s_angle[2];
  params_lock();
  g_params.att_offset[0] = s_offset[0]; g_params.att_offset[1] = s_offset[1]; g_params.att_offset[2] = s_offset[2];
  params_unlock();
  params_save();
}

// ---------------------------------------------------------------- ADS1015 (sun sensors)
static Adafruit_ADS1015 s_ads;
static bool s_ads_present = false;

static bool ads_init() {
  I2cGuard g;
  if (!s_ads.begin(0x48, &Wire)) return false;
  s_ads.setGain(GAIN_ONE);   // +-4.096 V: matches a 3.3 V rail far better than the library default (+-6.144 V)
  return true;
}
static void ads_read(SunData& out) {
  I2cGuard g;
  for (int i = 0; i < 4; i++) out.raw[i] = s_ads.readADC_SingleEnded(i);
  for (int i = 0; i < 4; i++) out.volts[i] = out.raw[i] * (4.096f / 2047.0f);
  out.valid = true;
  fdir_dev_ok(DEV_ADS1015);
}

// ---------------------------------------------------------------- INA3221 (power)
#include <Beastdevices_INA3221.h>
static Beastdevices_INA3221 s_ina(INA3221_ADDR40_GND);
static bool s_ina_present = false;

static bool ina_init() {
  I2cGuard g;
  if (!i2c_probe(0x40)) return false;
  s_ina.begin(&Wire);
  s_ina.setShuntRes(100, 100, 100);
  s_ina.setAveragingMode(INA3221_REG_CONF_AVG_64);   // stock firmware read one unaveraged sample;
  s_ina.setShuntConversionTime(INA3221_REG_CONF_CT_1100US);   // 64 x 1.1 ms ~= 70 ms per channel: still << 500 ms cadence
  s_ina.setBusConversionTime(INA3221_REG_CONF_CT_1100US);
  return true;
}
static void ina_read(PowerData& out) {
  I2cGuard g;
  float ibat = s_ina.getCurrent(INA3221_CH1) * 1000.0f;   // mA
  float vbat = s_ina.getVoltage(INA3221_CH1);
  if (g_params.ina_vbus_correction) vbat += (ibat / 1000.0f) * 0.1f;   // bus is sensed after the 100 mOhm shunt
  out.batt_v = vbat; out.batt_ma = ibat;
  out.panel_left_ma = s_ina.getCurrent(INA3221_CH2) * 1000.0f;
  out.panel_right_ma = s_ina.getCurrent(INA3221_CH3) * 1000.0f;
  out.panel_v = (s_ina.getVoltage(INA3221_CH2) + s_ina.getVoltage(INA3221_CH3)) / 2.0f;
  out.valid = true;
  fdir_dev_ok(DEV_INA3221);
}

// ---------------------------------------------------------------- DS3231 (RTC)
static RtcDS3231<TwoWire> s_rtc(Wire);
static bool s_rtc_present = false;

static bool rtc_init() {
  I2cGuard g;
  if (!i2c_probe(0x68)) return false;
  s_rtc.Begin();
  return s_rtc.GetIsRunning();
}
static void rtc_read(RtcData& out) {
  I2cGuard g;
  RtcDateTime dt = s_rtc.GetDateTime();
  if (!dt.IsValid()) { fdir_dev_error(DEV_RTC); out.valid = false; return; }
  memset(&out.t, 0, sizeof out.t);
  out.t.tm_year = dt.Year() - 1900; out.t.tm_mon = dt.Month() - 1; out.t.tm_mday = dt.Day();
  out.t.tm_hour = dt.Hour(); out.t.tm_min = dt.Minute(); out.t.tm_sec = dt.Second();
  out.temp_c = s_rtc.GetTemperature().AsFloatDegC();
  out.valid = true;
  fdir_dev_ok(DEV_RTC);
}

// ---------------------------------------------------------------- init + tasks
void sensors_init_all() {
  s_bme_present = bme_init();     fdir_dev(DEV_BME680).present = s_bme_present;
  s_imu_present = imu_init();     fdir_dev(DEV_IMU).present = s_imu_present;
  s_ads_present = ads_init();     fdir_dev(DEV_ADS1015).present = s_ads_present;
  s_ina_present = ina_init();     fdir_dev(DEV_INA3221).present = s_ina_present;
  s_rtc_present = rtc_init();     fdir_dev(DEV_RTC).present = s_rtc_present;

  RtcDateTime dt = s_rtc_present ? s_rtc.GetDateTime() : RtcDateTime();
  struct tm t{};
  if (s_rtc_present && dt.IsValid()) {
    t.tm_year = dt.Year() - 1900; t.tm_mon = dt.Month() - 1; t.tm_mday = dt.Day();
    t.tm_hour = dt.Hour(); t.tm_min = dt.Minute(); t.tm_sec = dt.Second();
  }
  clock_init_from_rtc(s_rtc_present && dt.IsValid(), &t);

  LOGI("SENSORS", "BME680=%d IMU=%d(who=0x%02X) ADS1015=%d INA3221=%d RTC=%d clock=%s",
       s_bme_present, s_imu_present, s_imu_whoami, s_ads_present, s_ina_present, s_rtc_present, clock_status_str());
}

static void imu_task(void*) {
  fdir_wdt_subscribe();
  const TickType_t period = pdMS_TO_TICKS(5);   // 200 Hz
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    if (s_imu_present) {
      I2cGuard g;
      imu_sample();
    }
    fdir_wdt_feed();
    vTaskDelayUntil(&last, period);
  }
}

static void sensors_task(void*) {
  fdir_wdt_subscribe();
  sensors_init_all();
  uint32_t seq = 0;
  for (;;) {
    imu_maybe_calibrate();

    Telemetry tm{};
    tm.seq = ++seq;
    tm.uptime_ms = millis();

    if (s_bme_present) bme_read(tm.env); else tm.env.valid = false;
    if (s_ads_present) ads_read(tm.sun); else tm.sun.valid = false;
    if (s_ina_present) ina_read(tm.pwr); else tm.pwr.valid = false;
    if (s_rtc_present) rtc_read(tm.rtc); else tm.rtc.valid = false;

    tm.att.valid = s_imu_present;
    tm.att.calibrated = s_calibrated;
    tm.att.roll_deg = s_angle[0] - s_offset[0];
    tm.att.pitch_deg = s_angle[1] - s_offset[1];
    tm.att.yaw_deg = s_angle[2] - s_offset[2];
    memcpy(tm.att.rate_dps, s_rate_dps, sizeof s_rate_dps);
    tm.att.rate_mag_dps = sqrtf(s_rate_dps[0]*s_rate_dps[0] + s_rate_dps[1]*s_rate_dps[1] + s_rate_dps[2]*s_rate_dps[2]);
    memcpy(tm.att.accel_g, s_accel_g, sizeof s_accel_g);
    tm.att.imu_temp_c = s_imu_temp_c;
    tm.att.imu_whoami = s_imu_whoami;
    tm.att.samples = s_imu_samples;

    telemetry_publish(tm);
    fdir_wdt_feed();
    vTaskDelay(pdMS_TO_TICKS(500));   // housekeeping-rate snapshot; attitude itself updates at 200 Hz above
  }
}

void sensors_task_start() {
  xTaskCreatePinnedToCore(imu_task, "imu", 3072, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(sensors_task, "sensors", 6144, nullptr, 3, nullptr, 1);
}
