// Host-native tests for the portable shared/ headers: crc, params table, command tokenizer.
// Build & run: pio test -e native   (uses PlatformIO's "native" platform, i.e. plain g++ --
// no ESP32/AVR toolchain needed, so this suite runs anywhere including offline sandboxes).
#include <unity.h>
#include <string.h>
#include "crc.h"
#include "params_def.h"
#include "cmdline.h"
#include "attitude_trigger.h"

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

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
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
