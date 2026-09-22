#include "apps/console.h"
#include "core/bus.h"
#include "core/log.h"
#include "core/events.h"
#include "core/fdir.h"
#include "core/mission_clock.h"
#include "core/params_store.h"
#include "hal/leds.h"
#include "hal/nano_link.h"
#include "apps/sensors.h"
#include "apps/data_logger.h"
#include "apps/camera_app.h"
#include "cmdline.h"
#include "mysat_icd.h"
#include "apps/wifi.h"
#include "apps/mission.h"
#include "apps/demo.h"
#include "attitude_trigger.h"
#include <LittleFS.h>
#include <strings.h>

static uint32_t s_frame_counter = 0;

static void print_env_line(Print& out, const EnvData& e) {
  if (!e.valid) { out.println(F("  environment: warming up / not present")); return; }
  out.printf("  env   T=%.1fC  RH=%.0f%%  P=%.1fhPa  gas=%.0fKOhm  IAQ=%.0f(acc%u)\n",
             e.temperature_c, e.humidity_pct, e.pressure_hpa, e.gas_kohm, e.iaq, e.iaq_accuracy);
}
static void print_att_line(Print& out, const AttitudeData& a) {
  if (!a.valid) { out.println(F("  attitude: IMU not present")); return; }
  out.printf("  att   roll=%+.1f pitch=%+.1f yaw=%+.1f deg   |w|=%.1f dps   %s\n",
             a.roll_deg, a.pitch_deg, a.yaw_deg, a.rate_mag_dps, a.calibrated ? "" : "(uncalibrated)");
}
static void print_sun_line(Print& out, const SunData& s) {
  if (!s.valid) { out.println(F("  sun: ADS1015 not present")); return; }
  out.printf("  sun   left=%d back=%d right=%d front=%d\n", s.raw[0], s.raw[1], s.raw[2], s.raw[3]);
}
static void print_pwr_line(Print& out, const PowerData& p) {
  if (!p.valid) { out.println(F("  power: INA3221 not present")); return; }
  out.printf("  pwr   batt=%.2fV %.0fmA   panels=%.2fV L=%.0fmA R=%.0fmA\n",
             p.batt_v, p.batt_ma, p.panel_v, p.panel_left_ma, p.panel_right_ma);
}

void console_print_telemetry_frame(Print& out) {
  Telemetry tm; telemetry_get(tm);
  Housekeeping hk; hk_get(hk);
  s_frame_counter++;
  char ts[24]; clock_now_iso(ts, sizeof ts);
  out.printf("\n%s  frame %lu  uptime %lus  mode=%u  clock=%s  %s\n",
             g_params.callsign, (unsigned long)s_frame_counter, (unsigned long)(millis() / 1000),
             hk.mode, clock_status_str(), ts);
  print_env_line(out, tm.env);
  print_att_line(out, tm.att);
  print_sun_line(out, tm.sun);
  print_pwr_line(out, tm.pwr);
  char countdown[24] = "";
  if (mission_countdown_s()) snprintf(countdown, sizeof countdown, "  deploy in %lus", (unsigned long)mission_countdown_s());
  out.printf("  msn   %s%s  attitude=%s\n", mission_phase_str(), countdown, orientation_str(mission_orientation()));
  out.printf("  aux   %s  servo=%u  panels=%s\n", hk.aux_ok ? "ok" : "no link",
             hk.aux_ok ? hk.aux.servo_angle : 0, hk.panels_deployed ? "deployed" : "retracted");
  out.printf("  link  wifi=%s%s  ip=%s\n",
             hk.wifi_mode == 0 ? "off" : (hk.wifi_mode == 1 ? "station" : "access-point"),
             hk.wifi_connected ? " connected" : "", hk.ip);
  if (g_params.logging_enabled) out.printf("  log   active, period=%us, rows=%lu\n", g_params.log_period_s, (unsigned long)logger_row_count());
}

static void print_plotter_frame(Print& out) {
  Telemetry tm; telemetry_get(tm);
  switch (g_params.plotter_group) {
    case 0: out.printf("T:%.2f,P:%.2f,RH:%.2f,Gas:%.2f\n", tm.env.temperature_c, tm.env.pressure_hpa, tm.env.humidity_pct, tm.env.gas_kohm); break;
    case 1: out.printf("Roll:%.1f,Pitch:%.1f,Yaw:%.1f,RateMag:%.1f\n", tm.att.roll_deg, tm.att.pitch_deg, tm.att.yaw_deg, tm.att.rate_mag_dps); break;
    case 2: out.printf("ph1:%d,ph2:%d,ph3:%d,ph4:%d\n", tm.sun.raw[0], tm.sun.raw[1], tm.sun.raw[2], tm.sun.raw[3]); break;
    case 3: out.printf("BattV:%.2f,BattI:%.1f,PanelV:%.2f,PanelIL:%.1f,PanelIR:%.1f\n", tm.pwr.batt_v, tm.pwr.batt_ma, tm.pwr.panel_v, tm.pwr.panel_left_ma, tm.pwr.panel_right_ma); break;
  }
}

void console_print_help(Print& out) {
  out.println(F(
    "commands:\n"
    "  status                        one telemetry frame now\n"
    "  solar deploy|retract|toggle   drive the wings via AUX\n"
    "  solar angle <0-180>           arbitrary servo angle (phase-1 tracking hook)\n"
    "  mission status                phase, countdown, orientation, upright reference\n"
    "  mission separate              trigger the launch sequence without a power cycle\n"
    "  mission abort                 cancel a pending deployment\n"
    "  mission learn-upright         record the current attitude as 'this way up'\n"
    "  mission auto on|off           arm/disable the flip-to-stow triggers\n"
    "  demo                          the demonstration show: status and schedule\n"
    "  demo run | demo stop          run the show now / cancel it\n"
    "  demo on|off                   run the show when the launch pin is pulled\n"
    "  led toggle|blink              STAR LED\n"
    "  imu calibrate                 3 s gyro-bias calibration (hold still)\n"
    "  imu align                     zero roll/pitch/yaw at the current attitude\n"
    "  console text|plotter <0-3>    telemetry output mode\n"
    "  loglevel <0-3>                serial verbosity (0 error .. 3 debug)\n"
    "  log start <period_s>|stop|delete|list\n"
    "  callsign <name>\n"
    "  time set <ISO8601 UTC>        e.g. time set 2026-09-01T12:00:00\n"
    "  wifi mode off|sta|ap | wifi ssid <s> | wifi pass <s> | wifi apply\n"
    "  radio at on|off                HC-12 AT-mode via AUX SET pin\n"
    "  photo | photo list\n"
    "  events | events clear\n"
    "  fs                             LittleFS usage\n"
    "  params list | params get <k> | params set <k> <v> | params save\n"
    "  reboot\n"
  ));
}

bool console_execute(const String& lineIn, Print& out) {
  char buf[CMDLINE_MAX_LEN];
  strncpy(buf, lineIn.c_str(), sizeof buf - 1); buf[sizeof buf - 1] = 0;
  cmdline_apply_legacy_alias(buf, sizeof buf);

  char* argv[CMDLINE_MAX_ARGS];
  int argc = cmdline_tokenize(buf, argv, CMDLINE_MAX_ARGS);
  if (argc == 0) return true;
  char cmd[24]; strncpy(cmd, argv[0], sizeof cmd - 1); cmd[sizeof cmd - 1] = 0; str_tolower_inplace(cmd);
  const char* a1 = argc > 1 ? argv[1] : "";
  const char* a2 = argc > 2 ? argv[2] : "";
  leds_flash_activity();

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) { console_print_help(out); return true; }

  if (!strcmp(cmd, "status")) { console_print_telemetry_frame(out); return true; }

  if (!strcmp(cmd, "solar")) {
    bool deploy = !strcasecmp(a1, "deploy"), retract = !strcasecmp(a1, "retract"), toggle = !strcasecmp(a1, "toggle");
    bool angle = !strcasecmp(a1, "angle") && argc > 2;
    if (!deploy && !retract && !toggle && !angle) { out.println(F("usage: solar deploy|retract|toggle|angle <deg>")); return true; }
    // A human reaching for the wings outranks the demonstration show. Stopping it here, before the
    // branches below write the wing state, is what stops the show's own idea of that state from
    // landing on top of the operator's. Only a real actuation gets this far: a typo must not cost
    // the audience the routine.
    if (demo_running()) { demo_stop("operator took the servo"); out.println(F("demo: show stopped, you have the servo")); }
    // The AUX ignores a movement that arrives while a sweep is running -- it retargets the sweep in
    // progress without restarting the power window -- so a command landing mid-sweep leaves the
    // wings part-way while the table records them as done. Refuse it rather than pretend.
    if (aux_servo_busy()) { out.println(F("solar: servo still moving, try again in a couple of seconds")); return true; }
    if (deploy) { aux_send(AUX_CMD_MOTOR_OPEN, 0); params_lock(); g_params.panels_deployed = 1; params_unlock(); params_save(); out.println(F("solar: deploying")); }
    else if (retract) { aux_send(AUX_CMD_MOTOR_CLOSE, 0); params_lock(); g_params.panels_deployed = 0; params_unlock(); params_save(); out.println(F("solar: retracting")); }
    else if (toggle) { bool dep = !g_params.panels_deployed; aux_send(dep ? AUX_CMD_MOTOR_OPEN : AUX_CMD_MOTOR_CLOSE, 0); params_lock(); g_params.panels_deployed = dep; params_unlock(); params_save(); out.println(F("solar: toggled")); }
    else { uint8_t deg = atoi(a2); aux_send(AUX_CMD_SERVO_ANGLE, deg); out.printf("solar: angle -> %u\n", deg); }
    mission_note_manual_actuation();   // a human is driving: stop the automatic flip triggers
    out.println(F("(automatic orientation triggers suspended, re-arm with 'mission auto on')"));
    events_post(EV_MOTOR, g_params.panels_deployed, "solar %s", a1);
    return true;
  }

  if (!strcmp(cmd, "led")) {
    if (!strcasecmp(a1, "toggle")) { star_led_set(!star_led_get()); out.printf("led: %s\n", star_led_get() ? "on" : "off"); }
    else if (!strcasecmp(a1, "blink")) { star_led_blink_test(); out.println(F("led: blink test")); }
    else out.println(F("usage: led toggle|blink"));
    return true;
  }

  if (!strcmp(cmd, "imu")) {
    if (!strcasecmp(a1, "calibrate")) { sensors_request_calibration(); out.println(F("imu: calibrating, hold still 3 s...")); }
    else if (!strcasecmp(a1, "align")) { sensors_request_gravity_align(); out.println(F("imu: attitude zeroed")); }
    else out.println(F("usage: imu calibrate|align"));
    return true;
  }

  if (!strcmp(cmd, "console")) {
    if (!strcasecmp(a1, "text")) { params_lock(); g_params.console_mode = 0; params_unlock(); params_save(); out.println(F("console: text mode")); }
    else if (!strcasecmp(a1, "plotter")) { params_lock(); g_params.console_mode = 1; if (argc > 2) g_params.plotter_group = atoi(a2) % 4; params_unlock(); params_save(); out.println(F("console: plotter mode")); }
    else if (!strcasecmp(a1, "toggle")) { params_lock(); g_params.console_mode = (g_params.console_mode == 2) ? 0 : 2; params_unlock(); params_save(); out.printf("console: %s\n", g_params.console_mode == 2 ? "quiet" : "on"); }
    else out.println(F("usage: console text|plotter <0-3>|toggle"));
    return true;
  }

  if (!strcmp(cmd, "loglevel")) {
    if (argc > 1) { log_set_level(constrain(atoi(a1), 0, 3)); params_lock(); g_params.log_level = log_level(); params_unlock(); params_save(); }
    out.printf("loglevel: %u\n", log_level());
    return true;
  }

  if (!strcmp(cmd, "log")) {
    if (!strcasecmp(a1, "start")) { logger_start(argc > 2 ? atoi(a2) : g_params.log_period_s); out.println(F("log: started")); }
    else if (!strcasecmp(a1, "stop")) { logger_stop(); out.println(F("log: stopped")); }
    else if (!strcasecmp(a1, "delete")) { logger_delete_all(); out.println(F("log: all files deleted")); }
    else if (!strcasecmp(a1, "list")) { logger_list(out); }
    else out.println(F("usage: log start <period_s>|stop|delete|list"));
    return true;
  }

  if (!strcmp(cmd, "callsign")) {
    if (argc > 1) { params_lock(); params_set_str(g_params.callsign, sizeof g_params.callsign, a1); params_unlock(); params_save(); }
    out.printf("callsign: %s\n", g_params.callsign);
    return true;
  }

  if (!strcmp(cmd, "time")) {
    if (!strcasecmp(a1, "set") && argc > 2) {
      struct tm t;
      if (clock_parse_iso(a2, &t)) { clock_set_system(&t, CLOCK_SET_BY_CMD); events_post(EV_CLOCK, 0, "time set by command"); out.println(F("time: set")); }
      else out.println(F("time: could not parse, expected YYYY-MM-DDTHH:MM:SS"));
    } else { char iso[24]; clock_now_iso(iso, sizeof iso); out.printf("time: %s (%s)\n", iso, clock_status_str()); }
    return true;
  }

  if (!strcmp(cmd, "wifi")) {
    if (!strcasecmp(a1, "mode") && argc > 2) {
      uint8_t m = !strcasecmp(a2, "off") ? 0 : (!strcasecmp(a2, "sta") ? 1 : 2);
      params_lock(); g_params.wifi_mode = m; params_unlock(); params_save();
      out.println(F("wifi: mode set, run 'wifi apply' to reconnect"));
    } else if (!strcasecmp(a1, "ssid") && argc > 2) { params_lock(); params_set_str(g_params.wifi_ssid, sizeof g_params.wifi_ssid, a2); params_unlock(); params_save(); out.println(F("wifi: ssid set")); }
    else if (!strcasecmp(a1, "pass") && argc > 2) { params_lock(); params_set_str(g_params.wifi_pass, sizeof g_params.wifi_pass, a2); params_unlock(); params_save(); out.println(F("wifi: password set")); }
    else if (!strcasecmp(a1, "apply")) { wifi_apply_params(); out.println(F("wifi: reapplying")); }
    else out.println(F("usage: wifi mode off|sta|ap | wifi ssid <s> | wifi pass <s> | wifi apply"));
    return true;
  }

  if (!strcmp(cmd, "radio")) {
    if (!strcasecmp(a1, "at") && argc > 2) { bool on = !strcasecmp(a2, "on"); aux_send(AUX_CMD_RF_SET, on); out.printf("radio: AT mode %s\n", on ? "on" : "off"); }
    else if (!strcasecmp(a1, "at")) { aux_send(AUX_CMD_RF_SET, 1); out.println(F("radio: AT mode on (auto-exits after 60s)")); }
    else if (!strcasecmp(a1, "power") && argc > 2) { aux_send(AUX_CMD_RF_POWER, !strcasecmp(a2, "on")); out.println(F("radio: power set")); }
    else out.println(F("usage: radio at [on|off] | radio power on|off"));
    return true;
  }

  if (!strcmp(cmd, "mission")) {
    if (argc == 1 || !strcasecmp(a1, "status")) { mission_status(out); }
    else if (!strcasecmp(a1, "separate")) { mission_trigger_separation(); out.println(F("mission: separation triggered")); }
    else if (!strcasecmp(a1, "abort")) { mission_abort(); out.println(F("mission: pending deployment aborted")); }
    else if (!strcasecmp(a1, "learn-upright") || !strcasecmp(a1, "learn")) { mission_learn_upright(); out.println(F("mission: upright reference recorded")); }
    else if (!strcasecmp(a1, "auto") && argc > 2) { mission_set_auto(!strcasecmp(a2, "on")); out.printf("mission: automatic triggers %s\n", mission_auto_enabled() ? "armed" : "disabled"); }
    else out.println(F("usage: mission status|separate|abort|learn-upright|auto on|off"));
    return true;
  }

  if (!strcmp(cmd, "demo")) {
    if (argc == 1 || !strcasecmp(a1, "status")) { demo_status(out); }
    else if (!strcasecmp(a1, "plan")) { demo_print_plan(out); }
    else if (!strcasecmp(a1, "run") || !strcasecmp(a1, "start")) {
      if (mission_begin_demo("commanded")) out.println(F("demo: show started -- keep hands clear of the wings"));
      else out.println(F("demo: could not start (already running, or demo.* leaves nothing to do)"));
    } else if (!strcasecmp(a1, "stop") || !strcasecmp(a1, "abort")) {
      if (!demo_running()) out.println(F("demo: not running"));
      else { demo_stop("stopped by command"); out.println(F("demo: stopped (a sweep already under way still finishes)")); }
    } else if (!strcasecmp(a1, "on") || !strcasecmp(a1, "off")) {
      params_lock(); g_params.demo_enabled = !strcasecmp(a1, "on"); params_unlock(); params_save();
      out.printf("demo: %s\n", g_params.demo_enabled ? "armed -- pulling the launch pin runs the show"
                                                     : "disarmed -- pulling the launch pin deploys once");
    } else { out.println(F("usage: demo [status|plan] | demo run|stop | demo on|off")); return true; }
    return true;
  }

  if (!strcmp(cmd, "photo")) {
    if (!strcasecmp(a1, "list")) { out.println(camera_index_json()); return true; }
    if (!camera_present()) { out.println(F("photo: camera not present")); return true; }
    Telemetry tm; telemetry_get(tm);
    char ts[24]; clock_now_iso(ts, sizeof ts);
    bool sunFov = tm.sun.valid && (tm.sun.raw[3] > 300);   // front sensor bright-ish; a coarse flag only
    int id = camera_capture(ts, tm.att.roll_deg, tm.att.pitch_deg, tm.att.yaw_deg, sunFov);
    out.printf("photo: %s\n", id >= 0 ? String("saved #" + String(id)).c_str() : "failed");
    return true;
  }

  if (!strcmp(cmd, "events")) {
    if (!strcasecmp(a1, "clear")) { events_clear(); out.println(F("events: cleared")); }
    else events_dump(out, 200);
    return true;
  }

  if (!strcmp(cmd, "fs")) {
    out.printf("LittleFS: %u / %u bytes used (%.1f%%)\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes(),
               100.0f * LittleFS.usedBytes() / LittleFS.totalBytes());
    return true;
  }

  if (!strcmp(cmd, "params")) {
    if (!strcasecmp(a1, "list")) {
      for (size_t i = 0; i < PARAM_TABLE_LEN; i++) { char v[80]; params_get_str(g_params, PARAM_TABLE[i], v, sizeof v); out.printf("  %-24s = %s\n", PARAM_TABLE[i].name, v); }
    } else if (!strcasecmp(a1, "get") && argc > 2) {
      const ParamDesc* d = params_find(a2);
      if (!d) out.println(F("params: unknown key"));
      else { char v[80]; params_get_str(g_params, *d, v, sizeof v, true); out.println(v); }
    } else if (!strcasecmp(a1, "set") && argc > 3) {
      const ParamDesc* d = params_find(a2);
      if (!d) out.println(F("params: unknown key"));
      else if (!params_set_from_str(g_params, *d, argv[3])) out.println(F("params: value rejected (range/parse)"));
      else { params_save(); out.println(F("params: set")); }
    } else if (!strcasecmp(a1, "save")) { params_save(); out.println(F("params: saved")); }
    else out.println(F("usage: params list | get <k> | set <k> <v> | save"));
    return true;
  }

  if (!strcmp(cmd, "reboot")) { out.println(F("rebooting...")); delay(100); ESP.restart(); return true; }

  return false;
}

// ---------------------------------------------------------------- serial task
static void console_task(void*) {
  fdir_wdt_subscribe();
  String line;
  uint32_t lastFrame = 0;
  for (;;) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line.trim();
        if (line.length()) {
          console_lock();
          bool known = console_execute(line, Serial);
          if (!known) Serial.println(F("? unrecognized, try 'help'"));
          console_unlock();
          events_post(EV_CMD, known ? 1 : 0, "cmd: %s", line.c_str());
        }
        line = "";
      } else if (line.length() < CMDLINE_MAX_LEN - 1) line += c;
    }
    if (g_params.console_mode != 2 && millis() - lastFrame >= g_params.telemetry_period_ms) {
      lastFrame = millis();
      console_lock();
      if (g_params.console_mode == 0) console_print_telemetry_frame(Serial);
      else print_plotter_frame(Serial);
      console_unlock();
    }
    fdir_wdt_feed();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void console_init() { /* nothing to do yet; kept for symmetry with the other apps */ }
void console_task_start() { xTaskCreatePinnedToCore(console_task, "console", 4096, nullptr, 2, nullptr, 1); }
