// Parameter table: one CRC-protected struct replaces config.txt, callsign.txt, cal.dat and the
// EEPROM motor flag of the stock firmware. Portable (no Arduino types) so host tests can cover it.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "crc.h"

#define PARAMS_MAGIC   0x4D595341u   // 'MYSA'
#define PARAMS_VERSION 2

struct __attribute__((packed)) Params {
  uint32_t magic;
  uint16_t version;
  uint16_t size;                  // sizeof(Params) at save time

  // identity
  char     callsign[12];

  // wifi: 0 = off, 1 = station with AP fallback, 2 = AP only
  uint8_t  wifi_mode;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     ap_pass[17];           // "" = open access point
  uint8_t  sta_timeout_s;         // how long to try the station network before AP fallback

  // console / telemetry
  uint16_t telemetry_period_ms;   // text or plotter frame period
  uint8_t  console_mode;          // 0 text, 1 plotter, 2 quiet
  uint8_t  plotter_group;         // 0 env, 1 attitude, 2 sun, 3 power
  uint8_t  log_level;             // 0 error, 1 warn, 2 info, 3 debug
  uint16_t hk_period_s;           // housekeeping packet period

  // mission data logger
  uint8_t  logging_enabled;
  uint16_t log_period_s;

  // actuators
  uint8_t  panels_deployed;       // persisted wing state
  uint8_t  servo_open_angle;
  uint8_t  servo_closed_angle;

  // IMU calibration
  uint8_t  imu_calibrated;
  int16_t  gyro_bias[3];          // raw LSB
  float    att_offset[3];         // roll, pitch, yaw zero offsets (deg)

  // power
  uint16_t battery_capacity_mah;
  uint8_t  ina_vbus_correction;   // 1 = add I*Rshunt to the bus reading (bus is sensed after the shunt)

  // mission sequencer (see apps/mission.cpp)
  uint16_t deploy_inhibit_s;      // seconds from "separation" to automatic deployment.
                                  // Real CubeSats are required to wait 1800 s; the demo default
                                  // is short so the sequence is watchable on a desk.
  uint8_t  auto_deploy;           // 1 = deploy automatically after separation
  uint8_t  stow_on_flip;          // 1 = retract the panels when turned upside down
  uint8_t  deploy_on_upright;     // 1 = deploy again when turned back upright
  uint8_t  flip_hold_s;           // an orientation must persist this long before it counts
  uint8_t  actuation_gap_s;       // minimum seconds between two servo commands
  uint8_t  up_ref_valid;          // 1 = up_ref holds a learned "this way up" vector
  float    up_ref[3];             // accelerometer vector recorded while sitting upright and still
  float    gs_lat;
  float    gs_lon;
  uint32_t launch_epoch;          // unix seconds of "separation", 0 = not launched

  uint32_t crc;                   // crc32 over all preceding bytes
};

static inline void params_set_str(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  size_t n = 0;
  while (src && src[n] && n < cap - 1) { dst[n] = src[n]; n++; }
  dst[n] = 0;
}

static inline void params_set_defaults(Params& p) {
  memset(&p, 0, sizeof(p));
  p.magic = PARAMS_MAGIC; p.version = PARAMS_VERSION; p.size = sizeof(Params);
  params_set_str(p.callsign, sizeof p.callsign, "MYSAT-1");
  p.wifi_mode = 2;                       // AP only until a station network is configured
  params_set_str(p.ap_pass, sizeof p.ap_pass, "");
  p.sta_timeout_s = 20;
  p.telemetry_period_ms = 1500;
  p.console_mode = 0;
  p.plotter_group = 0;
  p.log_level = 2;
  p.hk_period_s = 10;
  p.logging_enabled = 0;
  p.log_period_s = 10;
  p.panels_deployed = 0;
  p.servo_open_angle = 10;
  p.servo_closed_angle = 170;
  p.imu_calibrated = 0;
  p.battery_capacity_mah = 2600;         // typical 18650; user to confirm
  p.ina_vbus_correction = 1;
  p.deploy_inhibit_s = 10;               // demo-friendly; set 1800 for the real CubeSat rule
  p.auto_deploy = 1;
  p.stow_on_flip = 1;
  p.deploy_on_upright = 1;
  p.flip_hold_s = 2;
  p.actuation_gap_s = 5;
  p.up_ref_valid = 0;
  p.gs_lat = 32.08f; p.gs_lon = 34.78f;  // Israel center default, until the user sets it
  p.launch_epoch = 0;
}

static inline uint32_t params_crc(const Params& p) {
  return crc32_ieee((const uint8_t*)&p, offsetof(Params, crc));
}

static inline void params_seal(Params& p) {
  p.magic = PARAMS_MAGIC; p.version = PARAMS_VERSION; p.size = sizeof(Params);
  p.crc = params_crc(p);
}

static inline bool params_check(const Params& p) {
  return p.magic == PARAMS_MAGIC && p.version == PARAMS_VERSION && p.size == sizeof(Params) &&
         p.crc == params_crc(p);
}

// Params is __attribute__((packed)), so multi-byte fields cannot be bound to a T& (misaligned
// reference) on some GCC targets -- clamp by value instead and let the caller write it back.
template <typename T> static inline T clamp_val(T v, T lo, T hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
#define PARAMS_CLAMP_FIELD(p, field, T, lo, hi) do { \
  T v_ = (p).field; T nv_ = clamp_val<T>(v_, (T)(lo), (T)(hi)); \
  if (nv_ != v_) { (p).field = nv_; changed = true; } \
} while (0)

// Force every field into a sane range; returns true if anything was changed.
static inline bool params_sanitize(Params& p) {
  bool changed = false;
  p.callsign[sizeof p.callsign - 1] = 0; p.wifi_ssid[sizeof p.wifi_ssid - 1] = 0;
  p.wifi_pass[sizeof p.wifi_pass - 1] = 0; p.ap_pass[sizeof p.ap_pass - 1] = 0;
  if (p.callsign[0] == 0) { params_set_str(p.callsign, sizeof p.callsign, "MYSAT-1"); changed = true; }
  PARAMS_CLAMP_FIELD(p, wifi_mode, uint8_t, 0, 2);
  PARAMS_CLAMP_FIELD(p, sta_timeout_s, uint8_t, 5, 120);
  PARAMS_CLAMP_FIELD(p, telemetry_period_ms, uint16_t, 200, 60000);
  PARAMS_CLAMP_FIELD(p, console_mode, uint8_t, 0, 2);
  PARAMS_CLAMP_FIELD(p, plotter_group, uint8_t, 0, 3);
  PARAMS_CLAMP_FIELD(p, log_level, uint8_t, 0, 3);
  PARAMS_CLAMP_FIELD(p, hk_period_s, uint16_t, 1, 3600);
  PARAMS_CLAMP_FIELD(p, logging_enabled, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, log_period_s, uint16_t, 1, 3600);
  PARAMS_CLAMP_FIELD(p, panels_deployed, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, servo_open_angle, uint8_t, 0, 180);
  PARAMS_CLAMP_FIELD(p, servo_closed_angle, uint8_t, 0, 180);
  PARAMS_CLAMP_FIELD(p, imu_calibrated, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, battery_capacity_mah, uint16_t, 500, 10000);
  PARAMS_CLAMP_FIELD(p, ina_vbus_correction, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, deploy_inhibit_s, uint16_t, 0, 7200);
  PARAMS_CLAMP_FIELD(p, auto_deploy, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, stow_on_flip, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, deploy_on_upright, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, flip_hold_s, uint8_t, 1, 60);
  PARAMS_CLAMP_FIELD(p, actuation_gap_s, uint8_t, 1, 120);
  PARAMS_CLAMP_FIELD(p, up_ref_valid, uint8_t, 0, 1);
  PARAMS_CLAMP_FIELD(p, gs_lat, float, -90.f, 90.f);
  PARAMS_CLAMP_FIELD(p, gs_lon, float, -180.f, 180.f);
  for (int i = 0; i < 3; i++) {
    float v = p.att_offset[i];
    if (!(v == v)) { p.att_offset[i] = 0; changed = true; }  // NaN guard
    float u = p.up_ref[i];
    if (!(u == u)) { p.up_ref[i] = 0; p.up_ref_valid = 0; changed = true; }
  }
  return changed;
}

// ---------------------------------------------------------------- name <-> field access
enum ParamType : uint8_t { PT_U8, PT_U16, PT_U32, PT_I16, PT_F32, PT_STR };

struct ParamDesc {
  const char* name;
  ParamType   type;
  uint16_t    offset;
  uint16_t    len;      // string capacity for PT_STR, else element count (1)
  float       minv, maxv;
  bool        secret;   // never printed in clear (passwords)
};

#define PD(name, field, type, minv, maxv) { name, type, (uint16_t)offsetof(Params, field), 1, minv, maxv, false }
#define PS(name, field, secret) { name, PT_STR, (uint16_t)offsetof(Params, field), (uint16_t)sizeof(((Params*)0)->field), 0, 0, secret }

static const ParamDesc PARAM_TABLE[] = {
  PS("callsign",            callsign, false),
  PD("wifi.mode",           wifi_mode, PT_U8, 0, 2),
  PS("wifi.ssid",           wifi_ssid, false),
  PS("wifi.pass",           wifi_pass, true),
  PS("ap.pass",             ap_pass, true),
  PD("wifi.sta_timeout_s",  sta_timeout_s, PT_U8, 5, 120),
  PD("tm.period_ms",        telemetry_period_ms, PT_U16, 200, 60000),
  PD("console.mode",        console_mode, PT_U8, 0, 2),
  PD("console.plotter",     plotter_group, PT_U8, 0, 3),
  PD("log.level",           log_level, PT_U8, 0, 3),
  PD("hk.period_s",         hk_period_s, PT_U16, 1, 3600),
  PD("logger.enabled",      logging_enabled, PT_U8, 0, 1),
  PD("logger.period_s",     log_period_s, PT_U16, 1, 3600),
  PD("panels.deployed",     panels_deployed, PT_U8, 0, 1),
  PD("servo.open_deg",      servo_open_angle, PT_U8, 0, 180),
  PD("servo.closed_deg",    servo_closed_angle, PT_U8, 0, 180),
  PD("imu.calibrated",      imu_calibrated, PT_U8, 0, 1),
  PD("batt.capacity_mah",   battery_capacity_mah, PT_U16, 500, 10000),
  PD("ina.vbus_corr",       ina_vbus_correction, PT_U8, 0, 1),
  PD("mission.deploy_inhibit_s", deploy_inhibit_s, PT_U16, 0, 7200),
  PD("mission.auto_deploy",      auto_deploy, PT_U8, 0, 1),
  PD("mission.stow_on_flip",     stow_on_flip, PT_U8, 0, 1),
  PD("mission.deploy_on_upright", deploy_on_upright, PT_U8, 0, 1),
  PD("mission.flip_hold_s",      flip_hold_s, PT_U8, 1, 60),
  PD("mission.actuation_gap_s",  actuation_gap_s, PT_U8, 1, 120),
  PD("imu.up_ref_valid",         up_ref_valid, PT_U8, 0, 1),
  PD("gs.lat",              gs_lat, PT_F32, -90, 90),
  PD("gs.lon",              gs_lon, PT_F32, -180, 180),
  PD("mission.launch_epoch", launch_epoch, PT_U32, 0, 4294967295.f),
};
#define PARAM_TABLE_LEN (sizeof(PARAM_TABLE) / sizeof(PARAM_TABLE[0]))

static inline bool str_ieq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return false;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

static inline const ParamDesc* params_find(const char* name) {
  for (size_t i = 0; i < PARAM_TABLE_LEN; i++) if (str_ieq(PARAM_TABLE[i].name, name)) return &PARAM_TABLE[i];
  return nullptr;
}

// Render a field as text. Secrets render as "***" unless reveal is set.
static inline void params_get_str(const Params& p, const ParamDesc& d, char* out, size_t cap, bool reveal = false) {
  const uint8_t* base = (const uint8_t*)&p + d.offset;
  switch (d.type) {
    case PT_U8:  snprintf(out, cap, "%u", (unsigned)*base); break;
    case PT_U16: { uint16_t v; memcpy(&v, base, 2); snprintf(out, cap, "%u", (unsigned)v); break; }
    case PT_U32: { uint32_t v; memcpy(&v, base, 4); snprintf(out, cap, "%lu", (unsigned long)v); break; }
    case PT_I16: { int16_t v; memcpy(&v, base, 2); snprintf(out, cap, "%d", (int)v); break; }
    case PT_F32: { float v; memcpy(&v, base, 4); snprintf(out, cap, "%.4f", (double)v); break; }
    case PT_STR:
      if (d.secret && !reveal) snprintf(out, cap, "%s", ((const char*)base)[0] ? "***" : "");
      else snprintf(out, cap, "%s", (const char*)base);
      break;
  }
}

// Parse text into a field with range checking. Returns false on a bad value.
static inline bool params_set_from_str(Params& p, const ParamDesc& d, const char* text) {
  uint8_t* base = (uint8_t*)&p + d.offset;
  if (d.type == PT_STR) { params_set_str((char*)base, d.len, text ? text : ""); return true; }
  if (!text || !*text) return false;
  char* end = nullptr;
  if (d.type == PT_F32) {
    float v = strtof(text, &end);
    if (end == text || *end) return false;
    if (v < d.minv || v > d.maxv) return false;
    memcpy(base, &v, 4); return true;
  }
  long long v = strtoll(text, &end, 0);
  if (end == text || *end) return false;
  if ((float)v < d.minv || (float)v > d.maxv) return false;
  switch (d.type) {
    case PT_U8:  { uint8_t x = (uint8_t)v; memcpy(base, &x, 1); break; }
    case PT_U16: { uint16_t x = (uint16_t)v; memcpy(base, &x, 2); break; }
    case PT_U32: { uint32_t x = (uint32_t)v; memcpy(base, &x, 4); break; }
    case PT_I16: { int16_t x = (int16_t)v; memcpy(base, &x, 2); break; }
    default: return false;
  }
  return true;
}
