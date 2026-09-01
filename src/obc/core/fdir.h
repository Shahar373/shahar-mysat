// Fault detection, isolation and recovery — phase 0 subset:
// task watchdog, reset forensics, boot counter, RTC-RAM black box, per-device health counters.
#pragma once
#include <Arduino.h>

struct BlackBox {            // lives in RTC slow memory: survives soft resets, not power loss
  uint32_t magic;
  uint32_t last_alive_ms;    // millis() at the last tick before the reset
  uint32_t last_epoch;       // unix time at the last tick (0 if unknown)
  uint8_t  last_mode;
  uint8_t  wdt_fired;        // set by the panic path when the task watchdog trips
  uint16_t reserved;
};

struct DeviceHealth {
  const char* name;
  bool     present;          // found at init
  bool     failed;           // isolated after too many consecutive errors
  uint32_t errors;
  uint16_t consecutive;
};

enum DeviceId : uint8_t { DEV_BME680 = 0, DEV_IMU, DEV_ADS1015, DEV_INA3221, DEV_RTC, DEV_CAMERA, DEV_AUX, DEV_COUNT };

void        fdir_early_init();                // first thing in setup(): capture reset reason, bump boot counter
const char* fdir_reset_reason_str();
uint32_t    fdir_boot_count();
const BlackBox& fdir_black_box_at_boot();     // snapshot taken before this boot overwrote it
void        fdir_tick(uint8_t mode);          // control tick: refresh the black box (RAM only, no flash wear)

void        fdir_wdt_start(uint32_t timeout_s);
void        fdir_wdt_subscribe();             // call once from each supervised task
void        fdir_wdt_feed();                  // call regularly from each supervised task

DeviceHealth& fdir_dev(DeviceId id);
void        fdir_dev_ok(DeviceId id);
void        fdir_dev_error(DeviceId id);      // isolates after N consecutive errors, posts events
bool        fdir_dev_usable(DeviceId id);     // present && !failed
