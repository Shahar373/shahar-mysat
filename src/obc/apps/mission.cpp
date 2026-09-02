#include "apps/mission.h"
#include "core/bus.h"
#include "core/events.h"
#include "core/fdir.h"
#include "core/log.h"
#include "core/mission_clock.h"
#include "core/params_store.h"
#include "hal/nano_link.h"
#include "attitude_trigger.h"
#include <esp_system.h>

static const uint32_t TICK_MS = 250;
static const uint32_t SETTLE_BEFORE_LEARN_MS = 3000;   // how long it must sit still to learn "up"
static const uint32_t DEPLOY_TRAVEL_MS = 4000;         // servo sweep (~1.6 s) plus margin
static const float    PANEL_CURRENT_EVIDENCE_MA = 3.0f;// panel current rise that corroborates a deploy

static uint8_t  s_phase = MPHASE_BOOT;
static uint32_t s_phase_since = 0;
static uint32_t s_separation_ms = 0;
static uint32_t s_last_actuation_ms = 0;
static bool     s_auto = true;
static uint8_t  s_orientation = ORIENT_UNKNOWN;
static uint32_t s_settled_since = 0;
static OrientationDebouncer s_debounce;
static float    s_panel_ma_before = 0.0f;
static float    s_panel_ma_after = 0.0f;
static bool     s_last_deploy_confirmed = false;

static void set_phase(uint8_t phase) {
  if (s_phase == phase) return;
  s_phase = phase;
  s_phase_since = millis();
  events_post(EV_PHASE, phase, "phase -> %s", mission_phase_str());
  LOGI("MISSION", "phase -> %s", mission_phase_str());
}

uint8_t mission_phase() { return s_phase; }
uint8_t mission_orientation() { return s_orientation; }
bool mission_auto_enabled() { return s_auto; }

const char* mission_phase_str() {
  switch (s_phase) {
    case MPHASE_BOOT: return "BOOT";
    case MPHASE_PRELAUNCH: return "PRELAUNCH";
    case MPHASE_LEOP: return "LEOP";
    case MPHASE_DEPLOYING: return "DEPLOYING";
    case MPHASE_NOMINAL: return "NOMINAL";
    case MPHASE_STOWED: return "STOWED";
    case MPHASE_MANUAL: return "MANUAL";
    default: return "?";
  }
}

uint32_t mission_countdown_s() {
  if (s_phase != MPHASE_LEOP) return 0;
  uint32_t elapsed = (millis() - s_separation_ms) / 1000UL;
  uint16_t target = g_params.deploy_inhibit_s;
  return elapsed >= target ? 0 : (target - elapsed);
}

// ---------------------------------------------------------------- servo actions
static bool actuation_allowed() {
  return s_last_actuation_ms == 0 ||
         (millis() - s_last_actuation_ms) >= (uint32_t)g_params.actuation_gap_s * 1000UL;
}

static float panel_current_now() {
  Telemetry tm; telemetry_get(tm);
  if (!tm.pwr.valid) return 0.0f;
  float l = tm.pwr.panel_left_ma < 0 ? 0 : tm.pwr.panel_left_ma;
  float r = tm.pwr.panel_right_ma < 0 ? 0 : tm.pwr.panel_right_ma;
  return l + r;
}

static void begin_deploy(const char* reason) {
  s_panel_ma_before = panel_current_now();
  s_last_actuation_ms = millis();
  aux_send(AUX_CMD_MOTOR_OPEN, 0);
  events_post(EV_DEPLOY_START, 0, "deploy commanded (%s), panels drawing %.1f mA before", reason, s_panel_ma_before);
  LOGI("MISSION", "deploying solar panels (%s)", reason);
  set_phase(MPHASE_DEPLOYING);
}

// Confirmation comes from telemetry rather than from the fact that a command was sent: the AUX
// controller reports the angle its servo actually reached, and a rise in panel current is
// secondary evidence (only meaningful in a lit room, so it is logged, never used to fail).
static void finish_deploy() {
  s_panel_ma_after = panel_current_now();
  float delta = s_panel_ma_after - s_panel_ma_before;

  AuxStatus st;
  bool haveStatus = aux_read_status(st);
  int angleError = haveStatus ? (int)st.servo_angle - (int)g_params.servo_open_angle : 999;
  if (angleError < 0) angleError = -angleError;
  bool angleOk = haveStatus && st.servo_state == AUX_SERVO_OFF && angleError <= 15;

  params_lock(); g_params.panels_deployed = 1; params_unlock(); params_save();

  if (angleOk) {
    s_last_deploy_confirmed = true;
    events_post(EV_DEPLOY_CONFIRMED, (int32_t)delta,
                "deploy confirmed: servo at %u deg, panel current %+.1f mA", st.servo_angle, delta);
    LOGI("MISSION", "deployment confirmed (servo %u deg, panels %+.1f mA)", st.servo_angle, delta);
  } else if (haveStatus) {
    s_last_deploy_confirmed = false;
    events_post(EV_DEPLOY_UNCONFIRMED, (int32_t)delta, "deploy unconfirmed: servo at %u deg, expected %u",
                st.servo_angle, g_params.servo_open_angle);
    LOGW("MISSION", "deployment could not be confirmed: servo reports %u deg", st.servo_angle);
  } else {
    s_last_deploy_confirmed = false;
    events_post(EV_DEPLOY_UNCONFIRMED, (int32_t)delta, "deploy unconfirmed: no status readback from AUX");
    LOGW("MISSION", "deployment could not be confirmed (no AUX link)");
  }
  if (delta > PANEL_CURRENT_EVIDENCE_MA)
    LOGI("MISSION", "panel current rose %.1f mA after deployment", delta);

  set_phase(MPHASE_NOMINAL);
}

static void do_stow(const char* reason) {
  s_last_actuation_ms = millis();
  aux_send(AUX_CMD_MOTOR_CLOSE, 0);
  params_lock(); g_params.panels_deployed = 0; params_unlock(); params_save();
  events_post(EV_STOW, 0, "panels stowed (%s)", reason);
  LOGI("MISSION", "stowing solar panels (%s)", reason);
  set_phase(MPHASE_STOWED);
}

// ---------------------------------------------------------------- separation
void mission_trigger_separation() {
  s_separation_ms = millis();
  uint32_t epoch = clock_epoch_or_zero();
  if (epoch) { params_lock(); g_params.launch_epoch = epoch; params_unlock(); params_save(); }
  events_post(EV_SEPARATION, g_params.deploy_inhibit_s, "separation, deploying in %us", g_params.deploy_inhibit_s);
  LOGI("MISSION", "SEPARATION -- deployment in %u s", g_params.deploy_inhibit_s);
  set_phase(MPHASE_LEOP);
}

void mission_abort() {
  if (s_phase == MPHASE_LEOP) {
    LOGW("MISSION", "deployment aborted by command");
    events_post(EV_PHASE, 0, "deployment aborted by command");
    set_phase(g_params.panels_deployed ? MPHASE_NOMINAL : MPHASE_PRELAUNCH);
  }
}

void mission_set_auto(bool on) {
  s_auto = on;
  if (on && s_phase == MPHASE_MANUAL) set_phase(g_params.panels_deployed ? MPHASE_NOMINAL : MPHASE_STOWED);
  LOGI("MISSION", "automatic orientation triggers %s", on ? "armed" : "disabled");
}

void mission_note_manual_actuation() {
  s_last_actuation_ms = millis();
  if (s_phase == MPHASE_LEOP) LOGI("MISSION", "operator took control, pending deployment cancelled");
  set_phase(MPHASE_MANUAL);
}

// ---------------------------------------------------------------- upright reference
void mission_learn_upright() {
  Telemetry tm; telemetry_get(tm);
  Vec3 a = { tm.att.accel_g[0], tm.att.accel_g[1], tm.att.accel_g[2] };
  if (!v3_normalize(a)) { LOGW("MISSION", "cannot learn upright: accelerometer reads zero"); return; }
  params_lock();
  g_params.up_ref[0] = a.x; g_params.up_ref[1] = a.y; g_params.up_ref[2] = a.z;
  g_params.up_ref_valid = 1;
  params_unlock();
  params_save();
  s_debounce.reset((float)g_params.flip_hold_s);
  events_post(EV_UPREF_LEARNED, 0, "upright reference learned: [%.2f %.2f %.2f]", a.x, a.y, a.z);
  LOGI("MISSION", "upright reference learned: [%.2f %.2f %.2f]", a.x, a.y, a.z);
}

void mission_status(Print& out) {
  out.printf("mission phase   : %s (%lus in phase)\n", mission_phase_str(), (unsigned long)((millis() - s_phase_since) / 1000));
  if (s_phase == MPHASE_LEOP) out.printf("deploy in       : %lus\n", (unsigned long)mission_countdown_s());
  out.printf("orientation     : %s%s\n", orientation_str(s_orientation), g_params.up_ref_valid ? "" : " (no upright reference yet)");
  if (g_params.up_ref_valid)
    out.printf("upright ref     : [%.2f %.2f %.2f]\n", g_params.up_ref[0], g_params.up_ref[1], g_params.up_ref[2]);
  out.printf("panels          : %s%s\n", g_params.panels_deployed ? "deployed" : "stowed",
             s_phase == MPHASE_NOMINAL ? (s_last_deploy_confirmed ? " (confirmed)" : " (unconfirmed)") : "");
  out.printf("auto triggers   : %s (stow on flip %s, deploy on upright %s)\n",
             s_auto ? "armed" : "disabled",
             g_params.stow_on_flip ? "on" : "off", g_params.deploy_on_upright ? "on" : "off");
  out.printf("deploy inhibit  : %us after separation\n", g_params.deploy_inhibit_s);
  if (g_params.launch_epoch) {
    char met[24]; clock_format_duration(clock_met_s(g_params.launch_epoch), met, sizeof met);
    out.printf("mission elapsed : %s\n", met);
  }
}

// ---------------------------------------------------------------- task
static void mission_task(void*) {
  fdir_wdt_subscribe();
  s_debounce.reset((float)g_params.flip_hold_s);
  s_phase_since = millis();

  // Wait for the sensors task to publish at least once so the first orientation reading is real.
  vTaskDelay(pdMS_TO_TICKS(2000));

  // A power-on reset is the launch pin being pulled. A software or watchdog reset is not a new
  // launch -- the satellite is already in orbit, so it must not re-run the deployment sequence.
  esp_reset_reason_t reason = esp_reset_reason();
  bool poweron = (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT);
  if (poweron && g_params.auto_deploy) {
    mission_trigger_separation();
  } else {
    set_phase(g_params.panels_deployed ? MPHASE_NOMINAL : MPHASE_PRELAUNCH);
    if (!poweron) LOGI("MISSION", "not a power-on reset (%s): skipping the deployment sequence", fdir_reset_reason_str());
  }

  for (;;) {
    s_debounce.required_s = (float)g_params.flip_hold_s;   // picked up live from `params set`
    Telemetry tm; telemetry_get(tm);
    Vec3 accel = { tm.att.accel_g[0], tm.att.accel_g[1], tm.att.accel_g[2] };
    bool settled = tm.att.valid && at_is_settled(accel, tm.att.rate_mag_dps);

    // Learn "this way up" the first time the satellite is put down and left alone.
    if (settled) {
      if (s_settled_since == 0) s_settled_since = millis();
      if (!g_params.up_ref_valid && millis() - s_settled_since > SETTLE_BEFORE_LEARN_MS) mission_learn_upright();
    } else {
      s_settled_since = 0;
    }

    // Only judge orientation from a settled reading; while it is being carried, hold the last one.
    if (settled) {
      Vec3 ref = { g_params.up_ref[0], g_params.up_ref[1], g_params.up_ref[2] };
      uint8_t observed = at_classify(accel, ref, g_params.up_ref_valid != 0);
      if (s_debounce.update(observed, TICK_MS / 1000.0f)) {
        s_orientation = s_debounce.stable;
        events_post(EV_ORIENTATION, s_orientation, "orientation: %s", orientation_str(s_orientation));

        if (s_auto && actuation_allowed()) {
          if (s_orientation == ORIENT_INVERTED && g_params.stow_on_flip &&
              (s_phase == MPHASE_NOMINAL || s_phase == MPHASE_LEOP)) {
            if (s_phase == MPHASE_LEOP) LOGI("MISSION", "turned over during the countdown, deployment cancelled");
            do_stow("turned upside down");
          } else if (s_orientation == ORIENT_UPRIGHT && g_params.deploy_on_upright && s_phase == MPHASE_STOWED) {
            begin_deploy("turned back upright");
          }
        }
      }
    }

    switch (s_phase) {
      case MPHASE_LEOP:
        if (mission_countdown_s() == 0) begin_deploy("separation countdown complete");
        break;
      case MPHASE_DEPLOYING:
        if (millis() - s_phase_since >= DEPLOY_TRAVEL_MS) finish_deploy();
        break;
      default: break;
    }

    fdir_wdt_feed();
    vTaskDelay(pdMS_TO_TICKS(TICK_MS));
  }
}

void mission_task_start() { xTaskCreatePinnedToCore(mission_task, "mission", 4096, nullptr, 3, nullptr, 1); }
