#include "apps/demo.h"
#include "core/bus.h"
#include "core/events.h"
#include "core/log.h"
#include "core/params_store.h"
#include "hal/leds.h"
#include "hal/nano_link.h"
#include "apps/mission.h"
#include "mysat_icd.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// demo_tick() runs in the mission task, but demo_start()/demo_stop() are also reachable from the
// console and web tasks. Without this lock a stop arriving mid-tick could let one more servo
// command out after the show was already finished.
static SemaphoreHandle_t s_lock;
struct DemoGuard {
  DemoGuard() { if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex(); xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
  ~DemoGuard() { xSemaphoreGiveRecursive(s_lock); }
};

static DemoShowCfg s_cfg;
static DemoStep    s_steps[DEMO_MAX_STEPS];
static int         s_nsteps = 0;
static bool        s_running = false;
static uint32_t    s_t0 = 0;
static int         s_cur = -1;            // index of the step currently being executed
static uint32_t    s_runs = 0;            // shows completed since boot
static uint8_t     s_moves_done = 0;      // servo sweeps issued in this run
static uint8_t     s_moves_confirmed = 0; // ... of which the AUX reported the expected angle
static bool        s_panels_out = false;  // what the show believes the wings are doing
static uint32_t    s_last_servo_ms = 0;   // when the last wing command went out (valid if s_moves_done)

// Rebuilds the schedule from the parameter table. Cheap, so it runs at every start: editing
// `demo.*` with `params set` takes effect on the next show without a reboot.
static void reload_cfg() {
  params_get_demo_cfg(g_params, s_cfg);
  demo_cfg_sanitize(s_cfg);
  s_nsteps = demo_build_steps(s_cfg, s_steps, DEMO_MAX_STEPS);
  if (s_nsteps == 0) LOGE("DEMO", "schedule does not fit in %d steps, show disabled", DEMO_MAX_STEPS);
}

void demo_init() {
  if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
  reload_cfg();
}
bool demo_running() { return s_running; }
uint8_t demo_step_kind() { return (s_running && s_cur >= 0 && s_cur < s_nsteps) ? s_steps[s_cur].kind : DEMO_STEP_NONE; }

uint32_t demo_remaining_s() {
  if (!s_running) return 0;
  uint32_t total = demo_total_ms(s_steps, s_nsteps);
  uint32_t el = millis() - s_t0;
  return el >= total ? 0 : (total - el + 999) / 1000;
}

// The servo has no position feedback, so "confirmed" means the AUX controller reports having
// finished at the angle we asked for. It is evidence, not proof: if the wings are held by hand the
// AUX still reports the commanded angle. The panel current in the telemetry frame is the other
// half of the story, which is why it is logged next to it.
static void confirm_move(uint8_t kind, uint8_t index) {
  uint8_t want = (kind == DEMO_STEP_PANEL_OPEN) ? g_params.servo_open_angle : g_params.servo_closed_angle;
  AuxStatus st;
  if (!aux_read_status(st)) {
    LOGW("DEMO", "move %u (%s): no status readback from AUX", index, demo_step_str(kind));
    return;
  }
  int err = (int)st.servo_angle - (int)want;
  if (err < 0) err = -err;
  if (st.servo_state == AUX_SERVO_OFF && err <= 15) {
    s_moves_confirmed++;
    LOGI("DEMO", "move %u (%s) confirmed at %u deg", index, demo_step_str(kind), st.servo_angle);
  } else {
    LOGW("DEMO", "move %u (%s) unconfirmed: servo at %u deg (wanted %u), state %u",
         index, demo_step_str(kind), st.servo_angle, want, st.servo_state);
  }
}

static void finish(const char* reason) {
  star_led_set(false);
  s_running = false;
  s_cur = -1;
  s_runs++;
  // One NVS write per show rather than one per sweep: the wing state is only interesting once the
  // routine has settled, and the parameter table is not a scratchpad.
  params_lock(); g_params.panels_deployed = s_panels_out ? 1 : 0; params_unlock();
  params_save();
  Telemetry tm; telemetry_get(tm);
  float panel_ma = tm.pwr.valid ? (tm.pwr.panel_left_ma + tm.pwr.panel_right_ma) : 0.0f;
  events_post(EV_DEMO, (int32_t)s_moves_confirmed, "demo show %s: %u/%u moves confirmed, wings %s, panels %.1f mA",
              reason, s_moves_confirmed, s_moves_done, s_panels_out ? "out" : "in", panel_ma);
  LOGI("DEMO", "show %s -- %u of %u servo moves confirmed, wings %s",
       reason, s_moves_confirmed, s_moves_done, s_panels_out ? "out" : "in");
}

static void enter_step(int i) {
  s_cur = i;
  const DemoStep& s = s_steps[i];
  switch (s.kind) {
    case DEMO_STEP_PANEL_OPEN:
    case DEMO_STEP_PANEL_CLOSE: {
      bool open = (s.kind == DEMO_STEP_PANEL_OPEN);
      if (!aux_send(open ? AUX_CMD_MOTOR_OPEN : AUX_CMD_MOTOR_CLOSE, 0))
        LOGW("DEMO", "cycle %u: AUX did not acknowledge the wing command, the wings will not move", s.index);
      s_last_servo_ms = millis();
      s_panels_out = open; s_moves_done++;
      // Mirror the wing state into the live table (RAM only, no NVS write until finish()) so the
      // telemetry frame and the dashboard tell the truth while the show is running.
      params_lock(); g_params.panels_deployed = open ? 1 : 0; params_unlock();
      mission_note_actuation();
      LOGI("DEMO", "cycle %u: wings %s", s.index, open ? "out" : "in");
      break;
    }
    case DEMO_STEP_LIGHT_ON:
      star_led_set(true);
      LOGI("DEMO", "flash %u of %u: front light on for %u ms", s.index, s_cfg.light_flashes, s_cfg.light_on_ms);
      break;
    case DEMO_STEP_LIGHT_OFF:
      star_led_set(false);
      break;
    default:
      break;
  }
}

bool demo_start(const char* reason) {
  DemoGuard g;
  if (s_running) { LOGW("DEMO", "show already running"); return false; }
  reload_cfg();
  if (s_nsteps <= 1) { LOGW("DEMO", "nothing to show with the current demo.* parameters"); return false; }

  s_running = true;
  s_t0 = millis();
  s_cur = -1;
  s_moves_done = 0;
  s_moves_confirmed = 0;
  s_panels_out = g_params.panels_deployed != 0;
  star_led_cancel_blink();
  star_led_set(false);

  uint32_t total = demo_total_ms(s_steps, s_nsteps);
  events_post(EV_DEMO, (int32_t)(total / 1000), "demo show started (%s): %ux wings, %ux light, %lus",
              reason, s_cfg.panel_cycles, s_cfg.light_flashes, (unsigned long)(total / 1000));
  LOGI("DEMO", "show started (%s): %u wing cycles then %u light flashes, %lu s total",
       reason, s_cfg.panel_cycles, s_cfg.light_flashes, (unsigned long)(total / 1000));
  enter_step(demo_step_at(s_steps, s_nsteps, 0));
  return true;
}

// Stopping cancels the remaining steps and turns the light off. It cannot abort a sweep that is
// already under way: the AUX drives the servo from its own timer once commanded, and there is no
// "halt" in the protocol -- adding one would mean leaving the mechanism part-way, which is worse
// for it than letting the sweep finish.
void demo_stop(const char* reason) {
  DemoGuard g;
  if (!s_running) return;
  finish(reason);
}

void demo_tick() {
  if (!s_running) return;        // cheap check first; the lock is only taken when there is work
  DemoGuard g;
  if (!s_running) return;
  uint32_t el = millis() - s_t0;
  int i = demo_step_at(s_steps, s_nsteps, el, s_cur < 0 ? 0 : s_cur);
  if (i == s_cur) return;

  // Advance one step per tick even when the clock has run past several of them, so a tick delayed
  // by a busy moment never skips a step: skipping a PANEL_CLOSE would leave the wings out while the
  // show still believes it folded them, and skipping a LIGHT_OFF would merge two flashes into one.
  if (i > s_cur + 1) i = s_cur + 1;

  // Catching up must not squeeze two wing commands together either. The schedule keeps them
  // AUX_SERVO_MIN_CMD_GAP_MS apart on paper; this holds the line at run time too, so if the task
  // ever stalls for longer than a whole step the show slips instead of the servo.
  if (demo_step_moves_servo(s_steps[i].kind) && s_moves_done > 0 &&
      millis() - s_last_servo_ms < AUX_SERVO_MIN_CMD_GAP_MS) return;

  if (s_cur >= 0 && demo_step_moves_servo(s_steps[s_cur].kind))
    confirm_move(s_steps[s_cur].kind, s_steps[s_cur].index);

  if (i < 0 || s_steps[i].kind == DEMO_STEP_DONE) { s_cur = i; finish("complete"); return; }
  enter_step(i);
}

void demo_print_plan(Print& out) {
  DemoShowCfg c; params_get_demo_cfg(g_params, c);
  bool clamped = demo_cfg_sanitize(c);
  DemoStep steps[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, steps, DEMO_MAX_STEPS);
  if (n == 0) { out.println(F("demo: schedule too long for the step table")); return; }
  out.printf("demo plan (%s%s):\n", g_params.demo_enabled ? "armed for the launch pin" : "manual only",
             clamped ? ", values clamped" : "");
  for (int i = 0; i < n; i++) {
    if (steps[i].kind == DEMO_STEP_DONE) break;
    out.printf("  %6lu ms  %-11s", (unsigned long)steps[i].start_ms, demo_step_str(steps[i].kind));
    if (steps[i].index) out.printf(" #%u", steps[i].index);
    out.printf("  (%lu ms)\n", (unsigned long)steps[i].duration_ms);
  }
  out.printf("  total %lu s, %d servo sweeps, closest two commands %lu ms apart (AUX needs %u)\n",
             (unsigned long)(demo_total_ms(steps, n) / 1000), demo_servo_move_count(steps, n),
             (unsigned long)demo_min_servo_gap_ms(steps, n), AUX_SERVO_MIN_CMD_GAP_MS);
}

void demo_status(Print& out) {
  out.printf("demo            : %s\n", s_running ? "RUNNING" : "idle");
  out.printf("on launch pin   : %s\n", g_params.demo_enabled ? "yes -- pulling the pin runs the show"
                                                             : "no -- pulling the pin deploys once");
  if (s_running) {
    int cur = s_cur;   // read once: the mission task moves it while this prints
    out.printf("step            : %s", demo_step_str(demo_step_kind()));
    if (cur >= 0 && cur < s_nsteps && s_steps[cur].index) out.printf(" #%u", s_steps[cur].index);
    out.printf("\ntime left       : %lus\n", (unsigned long)demo_remaining_s());
    out.printf("moves so far    : %u issued, %u confirmed\n", s_moves_done, s_moves_confirmed);
  }
  out.printf("shows this boot : %lu\n", (unsigned long)s_runs);
  demo_print_plan(out);
}
