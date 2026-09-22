// Host-native tests for the portable shared/ headers: crc, params table, command tokenizer.
// Build & run: pio test -e native   (uses PlatformIO's "native" platform, i.e. plain g++ --
// no ESP32/AVR toolchain needed, so this suite runs anywhere including offline sandboxes).
#include <unity.h>
#include <string.h>
#include "crc.h"
#include "params_def.h"
#include "cmdline.h"
#include "attitude_trigger.h"
#include "demo_show.h"

// ---------------------------------------------------------------- crc.h
void test_crc8_known_vector() {
  // CRC-8/SMBUS of "123456789" (ASCII) is a widely published test vector: 0xF4
  const uint8_t data[] = "123456789";
  TEST_ASSERT_EQUAL_HEX8(0xF4, crc8_smbus(data, 9));
}
void test_crc8_changes_on_bit_flip() {
  uint8_t a[] = { 0xA5, 0x10, 0x2A };
  uint8_t c1 = crc8_smbus(a, 3);
  a[1] ^= 0x01;
  TEST_ASSERT_NOT_EQUAL(c1, crc8_smbus(a, 3));
}
void test_crc32_known_vector() {
  // CRC-32 (IEEE 802.3) of "123456789" is the standard check value 0xCBF43926
  const uint8_t data[] = "123456789";
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_ieee(data, 9));
}
void test_crc16_known_vector() {
  // CRC-16/CCITT-FALSE of "123456789" is 0x29B1
  const uint8_t data[] = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt(data, 9));
}

// ---------------------------------------------------------------- params_def.h
void test_params_defaults_are_valid() {
  Params p; params_set_defaults(p); params_seal(p);
  TEST_ASSERT_TRUE(params_check(p));
}
void test_params_detects_corruption() {
  Params p; params_set_defaults(p); params_seal(p);
  TEST_ASSERT_TRUE(params_check(p));
  p.telemetry_period_ms = 999;   // corrupt a field without resealing
  TEST_ASSERT_FALSE(params_check(p));
}
void test_params_sanitize_clamps_out_of_range() {
  Params p; params_set_defaults(p);
  p.telemetry_period_ms = 5;     // below the 200 ms floor
  p.log_level = 200;             // above the 3 ceiling
  bool changed = params_sanitize(p);
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL_UINT16(200, p.telemetry_period_ms);
  TEST_ASSERT_EQUAL_UINT8(3, p.log_level);
}
void test_params_find_and_roundtrip_u16() {
  Params p; params_set_defaults(p);
  const ParamDesc* d = params_find("tm.period_ms");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_TRUE(params_set_from_str(p, *d, "2500"));
  char out[32]; params_get_str(p, *d, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("2500", out);
}
void test_params_set_rejects_out_of_range() {
  Params p; params_set_defaults(p);
  const ParamDesc* d = params_find("tm.period_ms");
  TEST_ASSERT_FALSE(params_set_from_str(p, *d, "999999"));   // above the 60000 ceiling
}
void test_params_set_rejects_garbage() {
  Params p; params_set_defaults(p);
  const ParamDesc* d = params_find("gs.lat");
  TEST_ASSERT_FALSE(params_set_from_str(p, *d, "not-a-number"));
}
void test_params_secret_hidden_unless_revealed() {
  Params p; params_set_defaults(p);
  params_set_str(p.wifi_pass, sizeof p.wifi_pass, "hunter2");
  const ParamDesc* d = params_find("wifi.pass");
  char hidden[16]; params_get_str(p, *d, hidden, sizeof hidden, false);
  TEST_ASSERT_EQUAL_STRING("***", hidden);
  char shown[16]; params_get_str(p, *d, shown, sizeof shown, true);
  TEST_ASSERT_EQUAL_STRING("hunter2", shown);
}
void test_params_unknown_key_not_found() {
  TEST_ASSERT_NULL(params_find("does.not.exist"));
}
void test_params_case_insensitive_lookup() {
  TEST_ASSERT_NOT_NULL(params_find("CALLSIGN"));
  TEST_ASSERT_NOT_NULL(params_find("CallSign"));
}

// ---------------------------------------------------------------- cmdline.h
void test_cmdline_basic_split() {
  char line[] = "solar deploy now";
  char* argv[CMDLINE_MAX_ARGS];
  int argc = cmdline_tokenize(line, argv, CMDLINE_MAX_ARGS);
  TEST_ASSERT_EQUAL_INT(3, argc);
  TEST_ASSERT_EQUAL_STRING("solar", argv[0]);
  TEST_ASSERT_EQUAL_STRING("deploy", argv[1]);
  TEST_ASSERT_EQUAL_STRING("now", argv[2]);
}
void test_cmdline_quoted_argument_with_spaces() {
  char line[] = "wifi ssid \"My Home Network\"";
  char* argv[CMDLINE_MAX_ARGS];
  int argc = cmdline_tokenize(line, argv, CMDLINE_MAX_ARGS);
  TEST_ASSERT_EQUAL_INT(3, argc);
  TEST_ASSERT_EQUAL_STRING("My Home Network", argv[2]);
}
void test_cmdline_extra_whitespace_collapses() {
  char line[] = "  led   toggle  ";
  char* argv[CMDLINE_MAX_ARGS];
  int argc = cmdline_tokenize(line, argv, CMDLINE_MAX_ARGS);
  TEST_ASSERT_EQUAL_INT(2, argc);
  TEST_ASSERT_EQUAL_STRING("led", argv[0]);
  TEST_ASSERT_EQUAL_STRING("toggle", argv[1]);
}
void test_cmdline_empty_line() {
  char line[] = "   ";
  char* argv[CMDLINE_MAX_ARGS];
  TEST_ASSERT_EQUAL_INT(0, cmdline_tokenize(line, argv, CMDLINE_MAX_ARGS));
}
void test_legacy_alias_rewrites_known_word() {
  char line[CMDLINE_MAX_LEN]; strcpy(line, "SolarDeploy");
  TEST_ASSERT_TRUE(cmdline_apply_legacy_alias(line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("solar deploy", line);
}
void test_legacy_alias_ignores_modern_command() {
  char line[CMDLINE_MAX_LEN]; strcpy(line, "solar deploy");
  TEST_ASSERT_FALSE(cmdline_apply_legacy_alias(line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("solar deploy", line);   // left untouched
}
void test_legacy_alias_case_insensitive() {
  char line[CMDLINE_MAX_LEN]; strcpy(line, "STARTLOGGING");
  TEST_ASSERT_TRUE(cmdline_apply_legacy_alias(line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("log start", line);
}

// ---------------------------------------------------------------- attitude_trigger.h
// The satellite folds its solar panels based on these decisions, so they are worth proving
// off-hardware. "Upright" is whatever orientation was learned, not a hard-coded axis.
static const Vec3 UP  = { 0.0f, 0.0f, 1.0f };     // learned reference: +Z is up on this build
static const Vec3 DOWN = { 0.0f, 0.0f, -1.0f };
static const Vec3 SIDE = { 1.0f, 0.0f, 0.0f };

void test_at_upright_matches_reference() {
  TEST_ASSERT_EQUAL_INT(ORIENT_UPRIGHT, at_classify(UP, UP, true));
}
void test_at_inverted_is_opposite_reference() {
  TEST_ASSERT_EQUAL_INT(ORIENT_INVERTED, at_classify(DOWN, UP, true));
}
void test_at_on_its_side_is_neither() {
  TEST_ASSERT_EQUAL_INT(ORIENT_SIDEWAYS, at_classify(SIDE, UP, true));
}
void test_at_works_with_any_learned_axis() {
  // the IMU's mounting is unknown, so a reference along -Y must behave exactly the same
  Vec3 ref = { 0.0f, -1.0f, 0.0f };
  Vec3 upright = { 0.05f, -0.99f, 0.02f };
  Vec3 flipped = { 0.0f, 1.0f, 0.0f };
  TEST_ASSERT_EQUAL_INT(ORIENT_UPRIGHT, at_classify(upright, ref, true));
  TEST_ASSERT_EQUAL_INT(ORIENT_INVERTED, at_classify(flipped, ref, true));
}
void test_at_unknown_without_reference() {
  TEST_ASSERT_EQUAL_INT(ORIENT_UNKNOWN, at_classify(UP, UP, false));
}
void test_at_tilted_45_degrees_still_reads_upright() {
  Vec3 tilted = { 0.707f, 0.0f, 0.707f };          // cos = 0.707, above the 0.5 threshold
  TEST_ASSERT_EQUAL_INT(ORIENT_UPRIGHT, at_classify(tilted, UP, true));
}
void test_at_settled_rejects_shaking() {
  Vec3 shaken = { 0.0f, 0.0f, 1.9f };              // ~1.9 g: being moved, not resting
  TEST_ASSERT_FALSE(at_is_settled(shaken, 1.0f));
  TEST_ASSERT_TRUE(at_is_settled(UP, 1.0f));
}
void test_at_settled_rejects_tumbling() {
  TEST_ASSERT_FALSE(at_is_settled(UP, 120.0f));    // 1 g but spinning fast
}
void test_at_orientation_cosine_signs() {
  TEST_ASSERT_TRUE(at_orientation_cosine(UP, UP) > 0.99f);
  TEST_ASSERT_TRUE(at_orientation_cosine(DOWN, UP) < -0.99f);
}
void test_debouncer_requires_hold_time() {
  OrientationDebouncer d; d.reset(2.0f);
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 0.5f));   // first sighting: clock starts at zero
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 0.5f));   // 0.5 s held
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 0.5f));   // 1.0 s
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 0.5f));   // 1.5 s
  TEST_ASSERT_TRUE(d.update(ORIENT_INVERTED, 0.5f));    // 2.0 s -> fires once
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 0.5f));   // stays put, no repeat
}
void test_debouncer_resets_on_flapping() {
  OrientationDebouncer d; d.reset(2.0f);
  d.update(ORIENT_INVERTED, 0.5f);
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 1.5f));   // 1.5 s of the 2 s accumulated
  d.update(ORIENT_UPRIGHT, 0.25f);                      // a brief wobble throws it all away
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 1.5f));   // back to the first sighting
  TEST_ASSERT_FALSE(d.update(ORIENT_INVERTED, 1.5f));   // 1.5 s again, still short
  TEST_ASSERT_TRUE(d.update(ORIENT_INVERTED, 0.6f));    // 2.1 s -> fires
}
void test_debouncer_fires_on_each_new_state() {
  OrientationDebouncer d; d.reset(1.0f);
  d.update(ORIENT_UPRIGHT, 1.0f);                       // sighting
  TEST_ASSERT_TRUE(d.update(ORIENT_UPRIGHT, 1.0f));     // held long enough -> deploy edge
  d.update(ORIENT_INVERTED, 1.0f);
  TEST_ASSERT_TRUE(d.update(ORIENT_INVERTED, 1.0f));    // -> stow edge
  d.update(ORIENT_UPRIGHT, 1.0f);
  TEST_ASSERT_TRUE(d.update(ORIENT_UPRIGHT, 1.0f));     // -> deploy edge again, repeatable demo
}
void test_params_mission_defaults_are_demo_friendly() {
  Params p; params_set_defaults(p);
  TEST_ASSERT_EQUAL_UINT16(10, p.deploy_inhibit_s);     // watchable on a desk, not the real 1800 s
  TEST_ASSERT_EQUAL_UINT8(1, p.auto_deploy);
  TEST_ASSERT_EQUAL_UINT8(1, p.stow_on_flip);
  TEST_ASSERT_EQUAL_UINT8(0, p.up_ref_valid);           // nothing learned until it sits still
}
void test_params_mission_keys_are_settable() {
  Params p; params_set_defaults(p);
  const ParamDesc* d = params_find("mission.deploy_inhibit_s");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_TRUE(params_set_from_str(p, *d, "1800"));  // the real CubeSat inhibit
  TEST_ASSERT_EQUAL_UINT16(1800, p.deploy_inhibit_s);
  TEST_ASSERT_FALSE(params_set_from_str(p, *d, "99999"));
}


// ---------------------------------------------------------------- demo_show.h
// The demonstration show drives a servo, so its schedule is worth testing off-hardware: the
// interesting properties are "it does what the owner asked" and "it never commands the mechanism
// faster than the AUX controller can carry out".
static int demo_count_kind(const DemoStep* st, int n, uint8_t kind) {
  int c = 0;
  for (int i = 0; i < n; i++) if (st[i].kind == kind) c++;
  return c;
}

void test_demo_default_show_is_what_was_asked_for() {
  DemoShowCfg c; demo_cfg_defaults(c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  // wings out and back, twice
  TEST_ASSERT_EQUAL_INT(2, demo_count_kind(st, n, DEMO_STEP_PANEL_OPEN));
  TEST_ASSERT_EQUAL_INT(2, demo_count_kind(st, n, DEMO_STEP_PANEL_CLOSE));
  // then the front light on for three seconds, three times
  TEST_ASSERT_EQUAL_INT(3, demo_count_kind(st, n, DEMO_STEP_LIGHT_ON));
  for (int i = 0; i < n; i++)
    if (st[i].kind == DEMO_STEP_LIGHT_ON) TEST_ASSERT_EQUAL_UINT32(3000, st[i].duration_ms);
}

void test_demo_panels_finish_before_the_light_starts() {
  DemoShowCfg c; demo_cfg_defaults(c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  uint32_t lastServo = 0, firstLight = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) {
    if (demo_step_moves_servo(st[i].kind)) lastServo = st[i].start_ms + st[i].duration_ms;
    if (st[i].kind == DEMO_STEP_LIGHT_ON && st[i].start_ms < firstLight) firstLight = st[i].start_ms;
  }
  TEST_ASSERT_TRUE(firstLight >= lastServo);
}

// The reason the schedule exists at all: the AUX controller powers the servo for
// AUX_SERVO_POWER_MS and ignores a new movement while one is running, so two commands closer
// together than AUX_SERVO_MIN_CMD_GAP_MS give one half-finished sweep instead of two sweeps.
void test_demo_never_commands_the_servo_faster_than_aux_can_move_it() {
  DemoShowCfg c; demo_cfg_defaults(c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_TRUE(demo_min_servo_gap_ms(st, n) >= AUX_SERVO_MIN_CMD_GAP_MS);
}

void test_demo_sanitize_restores_the_servo_spacing() {
  DemoShowCfg c; demo_cfg_defaults(c);
  c.panel_move_ms = AUX_SERVO_POWER_MS;   // legal on its own...
  c.panel_rest_ms = 0;                    // ...but back-to-back this is too fast
  TEST_ASSERT_TRUE(demo_cfg_sanitize(c));
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_TRUE(demo_min_servo_gap_ms(st, n) >= AUX_SERVO_MIN_CMD_GAP_MS);
}

void test_demo_sanitize_clamps_absurd_values() {
  DemoShowCfg c; demo_cfg_defaults(c);
  c.panel_cycles = 250; c.light_flashes = 250; c.panel_move_ms = 10; c.light_on_ms = 0;
  TEST_ASSERT_TRUE(demo_cfg_sanitize(c));
  TEST_ASSERT_EQUAL_UINT8(DEMO_MAX_CYCLES, c.panel_cycles);
  TEST_ASSERT_EQUAL_UINT8(DEMO_MAX_FLASHES, c.light_flashes);
  TEST_ASSERT_TRUE(c.panel_move_ms >= AUX_SERVO_POWER_MS);
  TEST_ASSERT_TRUE(c.light_on_ms >= 50);
}

void test_demo_largest_allowed_show_still_fits_the_step_table() {
  DemoShowCfg c; demo_cfg_defaults(c);
  c.panel_cycles = DEMO_MAX_CYCLES; c.light_flashes = DEMO_MAX_FLASHES; c.end_deployed = 1;
  demo_cfg_sanitize(c);
  DemoStep st[DEMO_MAX_STEPS];
  TEST_ASSERT_GREATER_THAN_INT(0, demo_build_steps(c, st, DEMO_MAX_STEPS));  // 0 means it overflowed
}

void test_demo_step_lookup_walks_the_schedule_in_order() {
  DemoShowCfg c; demo_cfg_defaults(c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_EQUAL_INT(0, demo_step_at(st, n, 0));
  for (int i = 0; i < n; i++) {
    if (st[i].kind == DEMO_STEP_DONE) break;
    TEST_ASSERT_EQUAL_INT(i, demo_step_at(st, n, st[i].start_ms));                     // first ms
    TEST_ASSERT_EQUAL_INT(i, demo_step_at(st, n, st[i].start_ms + st[i].duration_ms - 1)); // last ms
  }
  // past the end it parks on DONE and stays there
  TEST_ASSERT_EQUAL_INT(n - 1, demo_step_at(st, n, demo_total_ms(st, n) + 60000));
  TEST_ASSERT_EQUAL_UINT8(DEMO_STEP_DONE, st[n - 1].kind);
}

// The runner passes the previous answer back as a hint so the usual "still the same step" case is
// one comparison. That optimisation must not change the answer, which is what this walks over.
void test_demo_step_lookup_hint_agrees_with_a_full_scan() {
  DemoShowCfg c; demo_cfg_defaults(c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  int hint = 0;
  for (uint32_t t = 0; t < demo_total_ms(st, n) + 2000; t += 37) {
    int scanned = demo_step_at(st, n, t, 0);
    hint = demo_step_at(st, n, t, hint);       // fed forward exactly as the runner does
    TEST_ASSERT_EQUAL_INT(scanned, hint);
  }
}

// Every millisecond of the show is covered by exactly one step, with no gap and no overlap --
// a hole would leave the runner executing nothing, an overlap would skip a servo command.
void test_demo_steps_tile_the_timeline_without_gaps() {
  DemoShowCfg c; demo_cfg_defaults(c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_EQUAL_UINT32(0, st[0].start_ms);
  for (int i = 1; i < n; i++)
    TEST_ASSERT_EQUAL_UINT32(st[i - 1].start_ms + st[i - 1].duration_ms, st[i].start_ms);
}

// The front light must not be left burning when the show ends -- the runner turns it off, and the
// schedule has to agree by finishing on an off step rather than an on one.
void test_demo_show_does_not_end_with_the_light_on() {
  DemoShowCfg c; demo_cfg_defaults(c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_FALSE(demo_step_light_on(st[n - 2].kind));
}

void test_demo_end_deployed_adds_a_final_sweep() {
  DemoShowCfg a; demo_cfg_defaults(a); a.end_deployed = 0;
  DemoShowCfg b; demo_cfg_defaults(b); b.end_deployed = 1;
  DemoStep sa[DEMO_MAX_STEPS], sb[DEMO_MAX_STEPS];
  int na = demo_build_steps(a, sa, DEMO_MAX_STEPS);
  int nb = demo_build_steps(b, sb, DEMO_MAX_STEPS);
  TEST_ASSERT_EQUAL_INT(demo_servo_move_count(sa, na) + 1, demo_servo_move_count(sb, nb));
  TEST_ASSERT_TRUE(demo_min_servo_gap_ms(sb, nb) >= AUX_SERVO_MIN_CMD_GAP_MS);
}

void test_demo_zero_cycles_leaves_only_the_light() {
  DemoShowCfg c; demo_cfg_defaults(c); c.panel_cycles = 0;
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_EQUAL_INT(0, demo_servo_move_count(st, n));
  TEST_ASSERT_EQUAL_INT(3, demo_count_kind(st, n, DEMO_STEP_LIGHT_ON));
}

// ---------------------------------------------------------------- params v2 -> v3 migration
// Adding the demo.* fields changed sizeof(Params), so every table written by the previous firmware
// fails params_check(). Without a migration path that silently costs the owner their callsign,
// WiFi credentials, gyro calibration and learned upright vector on the first boot after a flash.
static void build_fake_v2_blob(uint8_t* buf, const Params& src) {
  memcpy(buf, &src, PARAMS_V2_PAYLOAD_BYTES);
  uint16_t ver = 2, size = PARAMS_V2_SIZE_BYTES;
  memcpy(buf + offsetof(Params, version), &ver, sizeof ver);
  memcpy(buf + offsetof(Params, size), &size, sizeof size);
  uint32_t crc = crc32_ieee(buf, PARAMS_V2_PAYLOAD_BYTES);
  memcpy(buf + PARAMS_V2_PAYLOAD_BYTES, &crc, sizeof crc);
}

void test_params_migration_keeps_the_owners_settings() {
  Params old; params_set_defaults(old);
  params_set_str(old.callsign, sizeof old.callsign, "4X1ABC");
  params_set_str(old.wifi_ssid, sizeof old.wifi_ssid, "home-net");
  params_set_str(old.wifi_pass, sizeof old.wifi_pass, "hunter2");
  old.wifi_mode = 1; old.imu_calibrated = 1; old.gyro_bias[0] = -123; old.gyro_bias[2] = 45;
  old.up_ref_valid = 1; old.up_ref[0] = 0.0f; old.up_ref[1] = 0.0f; old.up_ref[2] = 1.0f;
  old.deploy_inhibit_s = 1800; old.panels_deployed = 1;

  uint8_t blob[PARAMS_V2_SIZE_BYTES];
  build_fake_v2_blob(blob, old);

  Params up;
  TEST_ASSERT_TRUE(params_try_migrate(up, blob, sizeof blob));
  TEST_ASSERT_TRUE(params_check(up));
  TEST_ASSERT_EQUAL_STRING("4X1ABC", up.callsign);
  TEST_ASSERT_EQUAL_STRING("home-net", up.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("hunter2", up.wifi_pass);
  TEST_ASSERT_EQUAL_UINT8(1, up.wifi_mode);
  TEST_ASSERT_EQUAL_UINT8(1, up.imu_calibrated);
  TEST_ASSERT_EQUAL_INT16(-123, up.gyro_bias[0]);
  TEST_ASSERT_EQUAL_INT16(45, up.gyro_bias[2]);
  TEST_ASSERT_EQUAL_UINT8(1, up.up_ref_valid);
  TEST_ASSERT_EQUAL_UINT16(1800, up.deploy_inhibit_s);
  TEST_ASSERT_EQUAL_UINT8(1, up.panels_deployed);
}

void test_params_migration_fills_demo_fields_with_defaults() {
  Params old; params_set_defaults(old);
  uint8_t blob[PARAMS_V2_SIZE_BYTES];
  build_fake_v2_blob(blob, old);
  Params up;
  TEST_ASSERT_TRUE(params_try_migrate(up, blob, sizeof blob));
  TEST_ASSERT_EQUAL_UINT8(0, up.demo_enabled);        // the show stays off until asked for
  TEST_ASSERT_EQUAL_UINT8(2, up.demo_panel_cycles);
  TEST_ASSERT_EQUAL_UINT8(3, up.demo_light_flashes);
  TEST_ASSERT_EQUAL_UINT16(3000, up.demo_light_on_ms);
}

void test_params_migration_rejects_a_corrupt_blob() {
  Params old; params_set_defaults(old);
  uint8_t blob[PARAMS_V2_SIZE_BYTES];
  build_fake_v2_blob(blob, old);
  blob[12] ^= 0x40;                                   // flip a bit inside the payload
  Params up; params_set_defaults(up);
  TEST_ASSERT_FALSE(params_try_migrate(up, blob, sizeof blob));
}

void test_params_migration_rejects_the_wrong_size() {
  Params cur; params_set_defaults(cur); params_seal(cur);
  Params up;
  TEST_ASSERT_FALSE(params_try_migrate(up, &cur, sizeof cur));   // a v3 table is not a v2 table
}

void test_params_demo_keys_are_settable() {
  Params p; params_set_defaults(p);
  const ParamDesc* d = params_find("demo.enabled");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_TRUE(params_set_from_str(p, *d, "1"));
  TEST_ASSERT_EQUAL_UINT8(1, p.demo_enabled);
  d = params_find("demo.light_on_ms");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_TRUE(params_set_from_str(p, *d, "5000"));
  TEST_ASSERT_EQUAL_UINT16(5000, p.demo_light_on_ms);
  d = params_find("demo.panel_move_ms");
  TEST_ASSERT_FALSE(params_set_from_str(p, *d, "10"));   // below the AUX servo window
}

// A stored table whose demo values would over-drive the servo has to come back safe, not fast.
void test_params_sanitize_fixes_unsafe_demo_timing() {
  Params p; params_set_defaults(p);
  p.demo_panel_move_ms = AUX_SERVO_POWER_MS;
  p.demo_panel_rest_ms = 0;
  TEST_ASSERT_TRUE(params_sanitize(p));
  DemoShowCfg c; params_get_demo_cfg(p, c);
  DemoStep st[DEMO_MAX_STEPS];
  int n = demo_build_steps(c, st, DEMO_MAX_STEPS);
  TEST_ASSERT_TRUE(demo_min_servo_gap_ms(st, n) >= AUX_SERVO_MIN_CMD_GAP_MS);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_demo_default_show_is_what_was_asked_for);
  RUN_TEST(test_demo_panels_finish_before_the_light_starts);
  RUN_TEST(test_demo_never_commands_the_servo_faster_than_aux_can_move_it);
  RUN_TEST(test_demo_sanitize_restores_the_servo_spacing);
  RUN_TEST(test_demo_sanitize_clamps_absurd_values);
  RUN_TEST(test_demo_largest_allowed_show_still_fits_the_step_table);
  RUN_TEST(test_demo_step_lookup_walks_the_schedule_in_order);
  RUN_TEST(test_demo_step_lookup_hint_agrees_with_a_full_scan);
  RUN_TEST(test_demo_steps_tile_the_timeline_without_gaps);
  RUN_TEST(test_demo_show_does_not_end_with_the_light_on);
  RUN_TEST(test_demo_end_deployed_adds_a_final_sweep);
  RUN_TEST(test_demo_zero_cycles_leaves_only_the_light);
  RUN_TEST(test_params_migration_keeps_the_owners_settings);
  RUN_TEST(test_params_migration_fills_demo_fields_with_defaults);
  RUN_TEST(test_params_migration_rejects_a_corrupt_blob);
  RUN_TEST(test_params_migration_rejects_the_wrong_size);
  RUN_TEST(test_params_demo_keys_are_settable);
  RUN_TEST(test_params_sanitize_fixes_unsafe_demo_timing);
  RUN_TEST(test_crc8_known_vector);
  RUN_TEST(test_crc8_changes_on_bit_flip);
  RUN_TEST(test_crc32_known_vector);
  RUN_TEST(test_crc16_known_vector);
  RUN_TEST(test_params_defaults_are_valid);
  RUN_TEST(test_params_detects_corruption);
  RUN_TEST(test_params_sanitize_clamps_out_of_range);
  RUN_TEST(test_params_find_and_roundtrip_u16);
  RUN_TEST(test_params_set_rejects_out_of_range);
  RUN_TEST(test_params_set_rejects_garbage);
  RUN_TEST(test_params_secret_hidden_unless_revealed);
  RUN_TEST(test_params_unknown_key_not_found);
  RUN_TEST(test_params_case_insensitive_lookup);
  RUN_TEST(test_cmdline_basic_split);
  RUN_TEST(test_cmdline_quoted_argument_with_spaces);
  RUN_TEST(test_cmdline_extra_whitespace_collapses);
  RUN_TEST(test_cmdline_empty_line);
  RUN_TEST(test_legacy_alias_rewrites_known_word);
  RUN_TEST(test_legacy_alias_ignores_modern_command);
  RUN_TEST(test_legacy_alias_case_insensitive);
  RUN_TEST(test_at_upright_matches_reference);
  RUN_TEST(test_at_inverted_is_opposite_reference);
  RUN_TEST(test_at_on_its_side_is_neither);
  RUN_TEST(test_at_works_with_any_learned_axis);
  RUN_TEST(test_at_unknown_without_reference);
  RUN_TEST(test_at_tilted_45_degrees_still_reads_upright);
  RUN_TEST(test_at_settled_rejects_shaking);
  RUN_TEST(test_at_settled_rejects_tumbling);
  RUN_TEST(test_at_orientation_cosine_signs);
  RUN_TEST(test_debouncer_requires_hold_time);
  RUN_TEST(test_debouncer_resets_on_flapping);
  RUN_TEST(test_debouncer_fires_on_each_new_state);
  RUN_TEST(test_params_mission_defaults_are_demo_friendly);
  RUN_TEST(test_params_mission_keys_are_settable);
  return UNITY_END();
}
