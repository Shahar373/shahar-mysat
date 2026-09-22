// Demo show: the fixed, repeatable sequence the satellite performs on the bench when the
// "remove before flight" pin is pulled, for showing the cube to visitors.
//
//     pin out (power applied) -> short arming hold -> the solar wings deploy and fold back
//     `panel_cycles` times -> the front light turns on for `light_on_ms`, `light_flashes` times.
//
// Only the *schedule* lives here: a pure list of timed steps computed from the configuration,
// with no Arduino, FreeRTOS or hardware dependency, so the host test suite can check it. The
// runner that actually drives the servo and the light is src/obc/apps/demo.cpp.
//
// Why a schedule rather than a chain of delays: a servo movement has a hardware constraint behind
// it. The AUX controller powers the servo for AUX_SERVO_POWER_MS and ignores a new movement while
// one is still running (see src/aux/main.cpp), so two commands sent too close together produce a
// half-finished sweep instead of two sweeps. Expressed as a list of steps that constraint becomes
// a property a test can check -- demo_min_servo_gap_ms() -- instead of a comment nobody re-reads.
#pragma once
#include <stdint.h>
#include "mysat_icd.h"

enum DemoStepKind : uint8_t {
  DEMO_STEP_NONE = 0,
  DEMO_STEP_ARM,          // powered, nothing moving yet: time to put the cube down and step back
  DEMO_STEP_PANEL_OPEN,   // servo commanded to the open angle
  DEMO_STEP_PANEL_CLOSE,  // servo commanded to the closed angle
  DEMO_STEP_PANEL_REST,   // servo unpowered between two sweeps
  DEMO_STEP_SETTLE,       // beat between the panel act and the light act
  DEMO_STEP_LIGHT_ON,     // STAR LED (front light) on
  DEMO_STEP_LIGHT_OFF,    // STAR LED off
  DEMO_STEP_DONE,
};

static inline const char* demo_step_str(uint8_t k) {
  switch (k) {
    case DEMO_STEP_ARM: return "arming";
    case DEMO_STEP_PANEL_OPEN: return "panels-out";
    case DEMO_STEP_PANEL_CLOSE: return "panels-in";
    case DEMO_STEP_PANEL_REST: return "servo-rest";
    case DEMO_STEP_SETTLE: return "settle";
    case DEMO_STEP_LIGHT_ON: return "light-on";
    case DEMO_STEP_LIGHT_OFF: return "light-off";
    case DEMO_STEP_DONE: return "done";
    default: return "idle";
  }
}

// True for the steps whose *start* issues a servo command. Used by the spacing check below.
static inline bool demo_step_moves_servo(uint8_t k) {
  return k == DEMO_STEP_PANEL_OPEN || k == DEMO_STEP_PANEL_CLOSE;
}
// True for the steps during which the front light is lit.
static inline bool demo_step_light_on(uint8_t k) { return k == DEMO_STEP_LIGHT_ON; }

struct DemoShowCfg {
  uint16_t arm_delay_ms;    // pin out -> first movement
  uint8_t  panel_cycles;    // deploy+fold pairs
  uint16_t panel_move_ms;   // time allowed for one sweep before the next step
  uint16_t panel_rest_ms;   // servo unpowered between sweeps
  uint16_t settle_ms;       // pause between the last sweep and the first flash
  uint8_t  light_flashes;
  uint16_t light_on_ms;
  uint16_t light_off_ms;
  uint8_t  end_deployed;    // 1 = one extra deploy at the end, so the show finishes wings-out
};

struct DemoStep {
  uint8_t  kind;
  uint8_t  index;        // 1-based cycle / flash number, 0 where it does not apply
  uint32_t start_ms;     // offset from the start of the show
  uint32_t duration_ms;
};

// Worst case with the clamped maxima below: 1 arm + 8*4 panel + 1 settle + 12*2 light + 2 = 60.
#define DEMO_MAX_STEPS   64
#define DEMO_MAX_CYCLES  8
#define DEMO_MAX_FLASHES 12

// What the owner asked for: wings out and back twice, then the front light on for three seconds,
// three times. Everything is a parameter so the show can be retimed from the console.
static inline void demo_cfg_defaults(DemoShowCfg& c) {
  c.arm_delay_ms  = 5000;
  c.panel_cycles  = 2;
  c.panel_move_ms = 2600;   // >= AUX_SERVO_POWER_MS: the sweep must finish before the next command
  c.panel_rest_ms = 400;
  c.settle_ms     = 1500;
  c.light_flashes = 3;
  c.light_on_ms   = 3000;
  c.light_off_ms  = 1000;
  c.end_deployed  = 0;
}

static inline uint16_t demo_clamp_u16(uint16_t v, uint16_t lo, uint16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Force the configuration into a range the mechanism survives. The floor on panel_move_ms +
// panel_rest_ms is the real constraint: it is what stops the show from cutting a sweep in half.
// Returns true if anything had to be changed.
static inline bool demo_cfg_sanitize(DemoShowCfg& c) {
  DemoShowCfg before = c;
  c.arm_delay_ms  = demo_clamp_u16(c.arm_delay_ms, 0, 60000);
  if (c.panel_cycles > DEMO_MAX_CYCLES) c.panel_cycles = DEMO_MAX_CYCLES;
  if (c.light_flashes > DEMO_MAX_FLASHES) c.light_flashes = DEMO_MAX_FLASHES;
  c.panel_move_ms = demo_clamp_u16(c.panel_move_ms, AUX_SERVO_POWER_MS, 20000);
  c.panel_rest_ms = demo_clamp_u16(c.panel_rest_ms, 0, 20000);
  if ((uint32_t)c.panel_move_ms + c.panel_rest_ms < AUX_SERVO_MIN_CMD_GAP_MS)
    c.panel_rest_ms = (uint16_t)(AUX_SERVO_MIN_CMD_GAP_MS - c.panel_move_ms);
  c.settle_ms    = demo_clamp_u16(c.settle_ms, 0, 60000);
  c.light_on_ms  = demo_clamp_u16(c.light_on_ms, 50, 60000);
  c.light_off_ms = demo_clamp_u16(c.light_off_ms, 50, 60000);
  if (c.end_deployed > 1) c.end_deployed = 1;
  return before.arm_delay_ms != c.arm_delay_ms || before.panel_cycles != c.panel_cycles ||
         before.panel_move_ms != c.panel_move_ms || before.panel_rest_ms != c.panel_rest_ms ||
         before.settle_ms != c.settle_ms || before.light_flashes != c.light_flashes ||
         before.light_on_ms != c.light_on_ms || before.light_off_ms != c.light_off_ms ||
         before.end_deployed != c.end_deployed;
}

// Expands the configuration into the ordered list of steps. Returns how many were written; 0 if
// `cap` is too small. The caller is expected to have sanitized `c` first.
static inline int demo_build_steps(const DemoShowCfg& c, DemoStep* out, int cap) {
  int n = 0;
  uint32_t t = 0;
  // push() silently stops at `cap`; the final count is checked against it before returning.
  #define DEMO_PUSH(K, IDX, DUR) do { \
    if (n < cap) { out[n].kind = (K); out[n].index = (uint8_t)(IDX); \
                   out[n].start_ms = t; out[n].duration_ms = (DUR); } \
    t += (DUR); n++; \
  } while (0)

  if (c.arm_delay_ms) DEMO_PUSH(DEMO_STEP_ARM, 0, c.arm_delay_ms);
  for (uint8_t i = 1; i <= c.panel_cycles; i++) {
    DEMO_PUSH(DEMO_STEP_PANEL_OPEN, i, c.panel_move_ms);
    if (c.panel_rest_ms) DEMO_PUSH(DEMO_STEP_PANEL_REST, i, c.panel_rest_ms);
    DEMO_PUSH(DEMO_STEP_PANEL_CLOSE, i, c.panel_move_ms);
    if (c.panel_rest_ms) DEMO_PUSH(DEMO_STEP_PANEL_REST, i, c.panel_rest_ms);
  }
  if (c.panel_cycles && c.light_flashes && c.settle_ms) DEMO_PUSH(DEMO_STEP_SETTLE, 0, c.settle_ms);
  for (uint8_t i = 1; i <= c.light_flashes; i++) {
    DEMO_PUSH(DEMO_STEP_LIGHT_ON, i, c.light_on_ms);
    DEMO_PUSH(DEMO_STEP_LIGHT_OFF, i, c.light_off_ms);
  }
  if (c.end_deployed) {
    if (c.settle_ms) DEMO_PUSH(DEMO_STEP_SETTLE, 0, c.settle_ms);
    DEMO_PUSH(DEMO_STEP_PANEL_OPEN, (uint8_t)(c.panel_cycles + 1), c.panel_move_ms);
  }
  DEMO_PUSH(DEMO_STEP_DONE, 0, 0);
  #undef DEMO_PUSH
  return n > cap ? 0 : n;
}

// Total run time, i.e. the start offset of the DEMO_STEP_DONE step.
static inline uint32_t demo_total_ms(const DemoStep* steps, int n) {
  return n > 0 ? steps[n - 1].start_ms : 0;
}

// Index of the step covering `elapsed_ms`, or n-1 (DONE) once the show is over. -1 for an empty
// list. Scanning from `hint` keeps the common "same step as last tick" case at one comparison.
static inline int demo_step_at(const DemoStep* steps, int n, uint32_t elapsed_ms, int hint = 0) {
  if (n <= 0) return -1;
  if (hint < 0 || hint >= n) hint = 0;
  for (int i = hint; i < n; i++)
    if (elapsed_ms < steps[i].start_ms + steps[i].duration_ms || steps[i].kind == DEMO_STEP_DONE)
      return i;
  return n - 1;
}

// Smallest interval between two consecutive servo commands in the schedule, or 0xFFFFFFFF when
// there are fewer than two. This is the number that has to stay >= AUX_SERVO_MIN_CMD_GAP_MS.
static inline uint32_t demo_min_servo_gap_ms(const DemoStep* steps, int n) {
  uint32_t worst = 0xFFFFFFFFu;
  int32_t prev = -1;
  for (int i = 0; i < n; i++) {
    if (!demo_step_moves_servo(steps[i].kind)) continue;
    if (prev >= 0) {
      uint32_t gap = steps[i].start_ms - steps[prev].start_ms;
      if (gap < worst) worst = gap;
    }
    prev = i;
  }
  return worst;
}

// How many servo sweeps the schedule commands -- the number that matters for mechanism wear.
static inline int demo_servo_move_count(const DemoStep* steps, int n) {
  int c = 0;
  for (int i = 0; i < n; i++) if (demo_step_moves_servo(steps[i].kind)) c++;
  return c;
}
