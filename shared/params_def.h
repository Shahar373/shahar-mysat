// Parameter table: one CRC-protected struct replaces config.txt, callsign.txt, cal.dat and the
// EEPROM motor flag of the stock firmware. Portable (no Arduino types) so host tests can cover it.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "crc.h"
#include "demo_show.h"

#define PARAMS_MAGIC   0x4D595341u   // 'MYSA'
#define PARAMS_VERSION 3

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

  // ---- appended in version 3: the bench demonstration show (shared/demo_show.h, apps/demo.cpp).
  // Everything from here to `crc` is new, which is what makes the v2 -> v3 upgrade a straight
  // prefix copy (params_try_migrate below) instead of a settings wipe.
  uint8_t  demo_enabled;          // 1 = pulling the launch pin runs the show, not the deployment
  uint8_t  demo_panel_cycles;     // wings out-and-back this many times
  uint8_t  demo_light_flashes;    // then the front light on/off this many times
  uint8_t  demo_end_deployed;     // 1 = finish the show with the wings out
  uint16_t demo_light_on_ms;
  uint16_t demo_light_off_ms;
  uint16_t demo_arm_delay_ms;     // pin out -> first movement
  uint16_t demo_panel_move_ms;    // time allowed for one servo sweep
  uint16_t demo_panel_rest_ms;    // servo unpowered between sweeps
  uint16_t demo_settle_ms;        // beat between the panel act and the light act
  uint8_t  demo_open_deg;         // wing angles the show drives to when the AUX takes an angle
  uint8_t  demo_closed_deg;       //   (v2 firmware); the stock firmware only knows a full sweep
  uint16_t demo_fade_ms;          // front light ramp, 0 = hard on/off

  uint32_t crc;                   // crc32 over all preceding bytes
};

// Layout of the version-2 table: identical up to the first demo field, then its own crc.
#define PARAMS_V2_PAYLOAD_BYTES ((uint16_t)offsetof(Params, demo_enabled))
#define PARAMS_V2_SIZE_BYTES    ((uint16_t)(PARAMS_V2_PAYLOAD_BYTES + sizeof(uint32_t)))

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
  // The demo show is off by default: pulling the pin keeps doing what docs/ARCHITECTURE.md
  // describes (one deployment) until someone deliberately turns the show on.
  DemoShowCfg d; demo_cfg_defaults(d);
  p.demo_enabled       = 0;
  p.demo_panel_cycles  = d.panel_cycles;
  p.demo_light_flashes = d.light_flashes;
  p.demo_end_deployed  = d.end_deployed;
  p.demo_light_on_ms   = d.light_on_ms;
  p.demo_light_off_ms  = d.light_off_ms;
  p.demo_arm_delay_ms  = d.arm_delay_ms;
  p.demo_panel_move_ms = d.panel_move_ms;
  p.demo_panel_rest_ms = d.panel_rest_ms;
  p.demo_settle_ms     = d.settle_ms;
  // 15 / 165 rather than the mechanism's 10 / 170: the servo then stops a few degrees short of
  // the end stops instead of stalling against them until the AUX cuts its power, which is the
  // wear the show would otherwise repeat four times per run. Visually the same sweep.
  p.demo_open_deg      = 15;
  p.demo_closed_deg    = 165;
  p.demo_fade_ms       = 300;
}

// Copy the demo fields out of / into a DemoShowCfg, so the show schedule and the stored table
// never drift apart.
static inline void params_get_demo_cfg(const Params& p, DemoShowCfg& c) {
  c.arm_delay_ms  = p.demo_arm_delay_ms;
  c.panel_cycles  = p.demo_panel_cycles;
  c.panel_move_ms = p.demo_panel_move_ms;
  c.panel_rest_ms = p.demo_panel_rest_ms;
  c.settle_ms     = p.demo_settle_ms;
  c.light_flashes = p.demo_light_flashes;
  c.light_on_ms   = p.demo_light_on_ms;
  c.light_off_ms  = p.demo_light_off_ms;
  c.end_deployed  = p.demo_end_deployed;
}
static inline void params_put_demo_cfg(Params& p, const DemoShowCfg& c) {
  p.demo_arm_delay_ms  = c.arm_delay_ms;
  p.demo_panel_cycles  = c.panel_cycles;
  p.demo_panel_move_ms = c.panel_move_ms;
  p.demo_panel_rest_ms = c.panel_rest_ms;
  p.demo_settle_ms     = c.settle_ms;
  p.demo_light_flashes = c.light_flashes;
  p.demo_light_on_ms   = c.light_on_ms;
  p.demo_light_off_ms  = c.light_off_ms;
  p.demo_end_deployed  = c.end_deployed;
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
  PARAMS_CLAMP_FIELD(p, demo_enabled, uint8_t, 0, 1);
  // The show's own limits live with the schedule, so the servo-spacing rule is enforced in one
  // place whether the values arrive from NVS, from `params set` or from a default.
  { DemoShowCfg d; params_get_demo_cfg(p, d);
    if (demo_cfg_sanitize(d)) { params_put_demo_cfg(p, d); changed = true; } }
  PARAMS_CLAMP_FIELD(p, demo_open_deg, uint8_t, 0, 180);
  PARAMS_CLAMP_FIELD(p, demo_closed_deg, uint8_t, 0, 180);
  PARAMS_CLAMP_FIELD(p, demo_fade_ms, uint16_t, 0, 2000);
  for (int i = 0; i < 3; i++) {
    float v = p.att_offset[i];
    if (!(v == v)) { p.att_offset[i] = 0; changed = true; }  // NaN guard
    float u = p.up_ref[i];
    if (!(u == u)) { p.up_ref[i] = 0; p.up_ref_valid = 0; changed = true; }
  }
  return changed;
}

// ---------------------------------------------------------------- name <-> field access
// Deliberately not named PT_U8/PT_STR/...: Arduino-ESP32's Preferences.h declares a
// PreferenceType enum with exactly those names, and params_store.cpp includes both headers.
enum ParamType : uint8_t { PARAM_U8, PARAM_U16, PARAM_U32, PARAM_I16, PARAM_F32, PARAM_STR };

struct ParamDesc {
  const char* name;
  ParamType   type;
  uint16_t    offset;
  uint16_t    len;      // string capacity for PARAM_STR, else element count (1)
  float       minv, maxv;
  bool        secret;   // never printed in clear (passwords)
};

#define PD(name, field, type, minv, maxv) { name, type, (uint16_t)offsetof(Params, field), 1, minv, maxv, false }
#define PS(name, field, secret) { name, PARAM_STR, (uint16_t)offsetof(Params, field), (uint16_t)sizeof(((Params*)0)->field), 0, 0, secret }

static const ParamDesc PARAM_TABLE[] = {
  PS("callsign",            callsign, false),
  PD("wifi.mode",           wifi_mode, PARAM_U8, 0, 2),
  PS("wifi.ssid",           wifi_ssid, false),
  PS("wifi.pass",           wifi_pass, true),
  PS("ap.pass",             ap_pass, true),
  PD("wifi.sta_timeout_s",  sta_timeout_s, PARAM_U8, 5, 120),
  PD("tm.period_ms",        telemetry_period_ms, PARAM_U16, 200, 60000),
  PD("console.mode",        console_mode, PARAM_U8, 0, 2),
  PD("console.plotter",     plotter_group, PARAM_U8, 0, 3),
  PD("log.level",           log_level, PARAM_U8, 0, 3),
  PD("hk.period_s",         hk_period_s, PARAM_U16, 1, 3600),
  PD("logger.enabled",      logging_enabled, PARAM_U8, 0, 1),
  PD("logger.period_s",     log_period_s, PARAM_U16, 1, 3600),
  PD("panels.deployed",     panels_deployed, PARAM_U8, 0, 1),
  PD("servo.open_deg",      servo_open_angle, PARAM_U8, 0, 180),
  PD("servo.closed_deg",    servo_closed_angle, PARAM_U8, 0, 180),
  PD("imu.calibrated",      imu_calibrated, PARAM_U8, 0, 1),
  PD("batt.capacity_mah",   battery_capacity_mah, PARAM_U16, 500, 10000),
  PD("ina.vbus_corr",       ina_vbus_correction, PARAM_U8, 0, 1),
  PD("mission.deploy_inhibit_s", deploy_inhibit_s, PARAM_U16, 0, 7200),
  PD("mission.auto_deploy",      auto_deploy, PARAM_U8, 0, 1),
  PD("mission.stow_on_flip",     stow_on_flip, PARAM_U8, 0, 1),
  PD("mission.deploy_on_upright", deploy_on_upright, PARAM_U8, 0, 1),
  PD("mission.flip_hold_s",      flip_hold_s, PARAM_U8, 1, 60),
  PD("mission.actuation_gap_s",  actuation_gap_s, PARAM_U8, 1, 120),
  PD("imu.up_ref_valid",         up_ref_valid, PARAM_U8, 0, 1),
  PD("gs.lat",              gs_lat, PARAM_F32, -90, 90),
  PD("gs.lon",              gs_lon, PARAM_F32, -180, 180),
  PD("mission.launch_epoch", launch_epoch, PARAM_U32, 0, 4294967295.f),
  PD("demo.enabled",        demo_enabled, PARAM_U8, 0, 1),
  PD("demo.panel_cycles",   demo_panel_cycles, PARAM_U8, 0, DEMO_MAX_CYCLES),
  PD("demo.light_flashes",  demo_light_flashes, PARAM_U8, 0, DEMO_MAX_FLASHES),
  PD("demo.end_deployed",   demo_end_deployed, PARAM_U8, 0, 1),
  PD("demo.light_on_ms",    demo_light_on_ms, PARAM_U16, 50, 60000),
  PD("demo.light_off_ms",   demo_light_off_ms, PARAM_U16, 50, 60000),
  PD("demo.arm_delay_ms",   demo_arm_delay_ms, PARAM_U16, 0, 60000),
  PD("demo.panel_move_ms",  demo_panel_move_ms, PARAM_U16, AUX_SERVO_POWER_MS, 20000),
  PD("demo.panel_rest_ms",  demo_panel_rest_ms, PARAM_U16, 0, 20000),
  PD("demo.settle_ms",      demo_settle_ms, PARAM_U16, 0, 60000),
  PD("demo.open_deg",       demo_open_deg, PARAM_U8, 0, 180),
  PD("demo.closed_deg",     demo_closed_deg, PARAM_U8, 0, 180),
  PD("demo.fade_ms",        demo_fade_ms, PARAM_U16, 0, 2000),
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
    case PARAM_U8:  snprintf(out, cap, "%u", (unsigned)*base); break;
    case PARAM_U16: { uint16_t v; memcpy(&v, base, 2); snprintf(out, cap, "%u", (unsigned)v); break; }
    case PARAM_U32: { uint32_t v; memcpy(&v, base, 4); snprintf(out, cap, "%lu", (unsigned long)v); break; }
    case PARAM_I16: { int16_t v; memcpy(&v, base, 2); snprintf(out, cap, "%d", (int)v); break; }
    case PARAM_F32: { float v; memcpy(&v, base, 4); snprintf(out, cap, "%.4f", (double)v); break; }
    case PARAM_STR:
      if (d.secret && !reveal) snprintf(out, cap, "%s", ((const char*)base)[0] ? "***" : "");
      else snprintf(out, cap, "%s", (const char*)base);
      break;
  }
}

// Parse text into a field with range checking. Returns false on a bad value.
static inline bool params_set_from_str(Params& p, const ParamDesc& d, const char* text) {
  uint8_t* base = (uint8_t*)&p + d.offset;
  if (d.type == PARAM_STR) { params_set_str((char*)base, d.len, text ? text : ""); return true; }
  if (!text || !*text) return false;
  char* end = nullptr;
  if (d.type == PARAM_F32) {
    float v = strtof(text, &end);
    if (end == text || *end) return false;
    if (v < d.minv || v > d.maxv) return false;
    memcpy(base, &v, 4); return true;
  }
  long long v = strtoll(text, &end, 0);
  if (end == text || *end) return false;
  if ((float)v < d.minv || (float)v > d.maxv) return false;
  switch (d.type) {
    case PARAM_U8:  { uint8_t x = (uint8_t)v; memcpy(base, &x, 1); break; }
    case PARAM_U16: { uint16_t x = (uint16_t)v; memcpy(base, &x, 2); break; }
    case PARAM_U32: { uint32_t x = (uint32_t)v; memcpy(base, &x, 4); break; }
    case PARAM_I16: { int16_t x = (int16_t)v; memcpy(base, &x, 2); break; }
    default: return false;
  }
  return true;
}

// Upgrade a stored version-2 table in place of a wipe. Adding fields changes sizeof(Params), so
// params_check() rejects every table written by the previous firmware -- which would silently
// throw away the owner's callsign, WiFi credentials, gyro calibration and learned upright vector
// on the first boot after a flash. Version 3 only appends, so the version-2 bytes are a valid
// prefix of the version-3 struct and can simply be copied over the defaults.
// Returns false (leaving `p` untouched) if `raw` is not a sound version-2 table.
static inline bool params_try_migrate(Params& p, const void* raw, size_t len) {
  if (!raw || len != PARAMS_V2_SIZE_BYTES) return false;
  const uint8_t* b = (const uint8_t*)raw;
  uint32_t magic; uint16_t ver, size, stored_off = PARAMS_V2_PAYLOAD_BYTES;
  memcpy(&magic, b + offsetof(Params, magic), sizeof magic);
  memcpy(&ver,   b + offsetof(Params, version), sizeof ver);
  memcpy(&size,  b + offsetof(Params, size), sizeof size);
  if (magic != PARAMS_MAGIC || ver != 2 || size != PARAMS_V2_SIZE_BYTES) return false;
  uint32_t stored_crc; memcpy(&stored_crc, b + stored_off, sizeof stored_crc);
  if (crc32_ieee(b, PARAMS_V2_PAYLOAD_BYTES) != stored_crc) return false;
  Params up; params_set_defaults(up);                    // new fields keep their defaults
  memcpy(&up, b, PARAMS_V2_PAYLOAD_BYTES);               // everything version 2 knew about
  up.magic = PARAMS_MAGIC; up.version = PARAMS_VERSION; up.size = sizeof(Params);
  params_sanitize(up);
  params_seal(up);
  p = up;
  return true;
}
