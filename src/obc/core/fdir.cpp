#include "core/fdir.h"
#include "core/log.h"
#include "core/events.h"
#include "core/mission_clock.h"
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <string.h>

#define BB_MAGIC 0xB1ACB0C5u
RTC_NOINIT_ATTR static BlackBox s_bb;
static BlackBox s_bb_at_boot;
static esp_reset_reason_t s_reason;
static uint32_t s_boots = 0;
static const uint16_t ISOLATE_AFTER = 5;

static DeviceHealth s_dev[DEV_COUNT] = {
  {"BME680", false, false, 0, 0}, {"IMU", false, false, 0, 0}, {"ADS1015", false, false, 0, 0},
  {"INA3221", false, false, 0, 0}, {"DS3231", false, false, 0, 0}, {"CAMERA", false, false, 0, 0},
  {"AUX", false, false, 0, 0},
};

void fdir_early_init() {
  s_reason = esp_reset_reason();
  if (s_bb.magic == BB_MAGIC) s_bb_at_boot = s_bb; else memset(&s_bb_at_boot, 0, sizeof s_bb_at_boot);
  memset(&s_bb, 0, sizeof s_bb); s_bb.magic = BB_MAGIC;

  Preferences p;
  if (p.begin("sys", false)) {
    s_boots = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", s_boots);
    p.end();
  }
}

const char* fdir_reset_reason_str() {
  switch (s_reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT_PIN";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

uint32_t fdir_boot_count() { return s_boots; }
const BlackBox& fdir_black_box_at_boot() { return s_bb_at_boot; }

void fdir_tick(uint8_t mode) {
  s_bb.last_alive_ms = millis();
  s_bb.last_epoch = clock_epoch_or_zero();
  s_bb.last_mode = mode;
}

void fdir_wdt_start(uint32_t timeout_s) {
  esp_task_wdt_init(timeout_s, true);      // panic -> reset -> reason TASK_WDT (arduino-esp32 2.x API)
}
void fdir_wdt_subscribe() { esp_task_wdt_add(NULL); }
void fdir_wdt_feed() { esp_task_wdt_reset(); }

DeviceHealth& fdir_dev(DeviceId id) { return s_dev[id]; }
bool fdir_dev_usable(DeviceId id) { return s_dev[id].present && !s_dev[id].failed; }

void fdir_dev_ok(DeviceId id) {
  DeviceHealth& d = s_dev[id];
  if (d.failed) { d.failed = false; events_post(EV_SENSOR_RECOVERED, id, "%s recovered", d.name); }
  d.consecutive = 0;
}

void fdir_dev_error(DeviceId id) {
  DeviceHealth& d = s_dev[id];
  d.errors++;
  if (++d.consecutive >= ISOLATE_AFTER && !d.failed) {
    d.failed = true;
    events_post(EV_SENSOR_FAIL, id, "%s isolated after %u consecutive errors", d.name, (unsigned)d.consecutive);
    LOGW("FDIR", "%s isolated", d.name);
  }
}
