/*
 * MYSAT OBC FIRMWARE v2.0.0-a0 — ESP32-CAM (AI Thinker)
 *
 * Phase-0 rewrite of the stock MySat firmware: FreeRTOS tasks instead of one superloop, no
 * command that blocks waiting for a human, a watchdog and reset forensics, a CRC-protected
 * parameter table, corrected sensor sampling (IMU dt, INA3221 averaging/Vbus), and a WiFi
 * manager that always leaves the satellite reachable (station with AP fallback, or AP-only for
 * a zero-setup standalone demo).
 *
 * See docs/ARCHITECTURE.md for the task diagram and docs/COMMANDS.md for the console grammar.
 */
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

#include "core/log.h"
#include "core/events.h"
#include "core/fdir.h"
#include "core/bus.h"
#include "core/mission_clock.h"
#include "core/params_store.h"
#include "hal/i2c_bus.h"
#include "hal/leds.h"
#include "hal/nano_link.h"
#include "apps/sensors.h"
#include "apps/console.h"
#include "apps/data_logger.h"
#include "apps/camera_app.h"
#include "apps/wifi.h"
#include "apps/web.h"
#include "apps/mission.h"
#include "mysat_icd.h"

static uint32_t s_boot_ms;

// The mission phase owns the LED while something is actually happening to the spacecraft;
// otherwise the light falls back to reporting the comms link, which is the next most useful thing
// to see from across the room.
static uint8_t current_led_state() {
  for (int i = 0; i < DEV_COUNT; i++) if (fdir_dev((DeviceId)i).failed) return LED_FAULT;
  if (millis() - s_boot_ms < 5000) return LED_BOOT;
  switch (mission_phase()) {
    case MPHASE_LEOP: return LED_LEOP;
    case MPHASE_DEPLOYING: return LED_DEPLOY;
    case MPHASE_STOWED: return LED_STOWED;
    default: break;
  }
  uint8_t mode; bool connected; int8_t rssi; char ip[16];
  wifi_get_status(mode, connected, rssi, ip, sizeof ip);
  if (mode == 1 && !connected) return LED_NO_LINK;
  if (mode == 2) return LED_AP_MODE;
  return LED_NOMINAL;
}

static void led_task(void*) {
  for (;;) { leds_set((LedState)current_led_state()); leds_tick(); star_led_tick(); vTaskDelay(pdMS_TO_TICKS(20)); }
}

static void control_task(void*) {
  fdir_wdt_subscribe();
  uint32_t lastHb = 0, lastEventFlush = 0;
  for (;;) {
    Telemetry tm; telemetry_get(tm);
    logger_tick(tm);

    if (millis() - lastHb > 5000) {   // AUX heartbeat + status readback
      lastHb = millis();
      aux_heartbeat(MODE_NOMINAL);
      AuxStatus st;
      Housekeeping hk; hk_get(hk);
      hk.aux_ok = aux_read_status(st);
      if (hk.aux_ok) hk.aux = st;
      hk.uptime_s = millis() / 1000UL;
      hk.boots = fdir_boot_count();
      hk.reset_reason = fdir_reset_reason_str();
      hk.heap_free = ESP.getFreeHeap();
      hk.heap_min = ESP.getMinFreeHeap();
      hk.psram_free = ESP.getPsramSize() ? ESP.getFreePsram() : 0;
      hk.cpu_temp_c = temperatureRead();
      hk.fs_used = LittleFS.usedBytes();
      hk.fs_total = LittleFS.totalBytes();
      hk.mode = (millis() - s_boot_ms < 5000) ? MODE_BOOT : MODE_NOMINAL;
      hk.mission_phase = mission_phase();
      hk.mission_countdown_s = mission_countdown_s();
      hk.orientation = mission_orientation();
      wifi_get_status(hk.wifi_mode, hk.wifi_connected, hk.wifi_rssi, hk.ip, sizeof hk.ip);
      hk.panels_deployed = g_params.panels_deployed;
      hk.star_led = star_led_get();
      hk.camera_ok = camera_present();
      hk_publish(hk);
      fdir_tick(hk.mode);
    }
    if (millis() - lastEventFlush > 2000) { lastEventFlush = millis(); events_flush(); }

    fdir_wdt_feed();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void setup() {
  s_boot_ms = millis();
  fdir_early_init();          // must run before anything else touches the black box / boot counter

  Serial.begin(115200);
  delay(50);
  log_init();
  log_set_level(LOG_INFO);
  bus_init();
  events_init();

  if (!LittleFS.begin(true)) { Serial.println(F("[FATAL] LittleFS mount failed even after format")); }

  params_load();
  log_set_level(g_params.log_level);

  Serial.println();
  Serial.println(F("=================================================="));
  Serial.printf("MYSAT OBC %s\n", MYSAT_FW_VERSION);
  Serial.printf("boot #%lu, reset reason: %s\n", (unsigned long)fdir_boot_count(), fdir_reset_reason_str());
  const BlackBox& bb = fdir_black_box_at_boot();
  if (bb.magic == 0xB1ACB0C5u) Serial.printf("previous tick: mode=%u, %lu ms before this boot\n", bb.last_mode, (unsigned long)bb.last_alive_ms);
  Serial.println(F("=================================================="));

  events_post(EV_BOOT, fdir_boot_count(), "boot, reset=%s", fdir_reset_reason_str());

  fdir_wdt_start(15);         // 15 s: generous enough for WiFi station connect attempts elsewhere
  i2c_init();
  leds_init();
  console_init();
  logger_init();
  if (!camera_init()) LOGW("MAIN", "camera not present or failed to init (non-fatal, secondary payload)");

  sensors_task_start();
  mission_task_start();
  console_task_start();
  wifi_task_start();
  web_task_start();
  xTaskCreatePinnedToCore(led_task, "leds", 2048, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(control_task, "control", 4096, nullptr, 2, nullptr, 1);

  fdir_wdt_subscribe();
  LOGI("MAIN", "setup complete");
}

void loop() {
  // Everything runs in FreeRTOS tasks; keep the Arduino loop task itself out of the way.
  fdir_wdt_feed();
  vTaskDelay(pdMS_TO_TICKS(1000));
}
