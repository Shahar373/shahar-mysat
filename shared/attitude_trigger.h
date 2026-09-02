// Orientation triggers: decide "the satellite is upright / upside down" from the accelerometer,
// compared against a learned reference vector. Kept portable (no Arduino types, no floats beyond
// the C library) so the logic can be unit tested on the host -- the mission sequencer actuates a
// servo from these decisions, so they are worth testing off-hardware.
//
// Why a *learned* reference instead of hard-coding "Z axis up": the IMU's orientation relative to
// the cube faces is not documented for this kit, and it differs between board revisions. The
// satellite records its own gravity vector while sitting still and calls that "upright"; every
// later decision is the angle between the current gravity vector and that reference. That works
// no matter how the sensor happens to be soldered.
#pragma once
#include <stdint.h>
#include <math.h>

struct Vec3 { float x, y, z; };

static inline float v3_dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_norm(const Vec3& v) { return sqrtf(v3_dot(v, v)); }

// Normalizes in place. Returns false (and leaves v untouched) if the vector is too short to have
// a meaningful direction, e.g. free fall or a dead sensor reading zeros.
static inline bool v3_normalize(Vec3& v) {
  float n = v3_norm(v);
  if (!(n > 1e-3f)) return false;
  v.x /= n; v.y /= n; v.z /= n;
  return true;
}

// True when the measured acceleration is plausibly just gravity: magnitude near 1 g and the body
// is not rotating quickly. Anything else means the satellite is being carried, shaken or tumbled,
// and its "which way is up" reading cannot be trusted.
static inline bool at_is_settled(const Vec3& accel_g, float rate_mag_dps,
                                 float g_tolerance = 0.25f, float rate_limit_dps = 25.0f) {
  float n = v3_norm(accel_g);
  if (n < 1.0f - g_tolerance || n > 1.0f + g_tolerance) return false;
  return fabsf(rate_mag_dps) <= rate_limit_dps;
}

enum Orientation : uint8_t {
  ORIENT_UNKNOWN = 0,   // no reference learned yet, or the reading is not usable
  ORIENT_UPRIGHT = 1,   // within upright_cos of the learned reference
  ORIENT_SIDEWAYS = 2,  // tipped onto a side face: neither trigger fires
  ORIENT_INVERTED = 3,  // more than ~120 degrees from the reference
};

static inline const char* orientation_str(uint8_t o) {
  switch (o) {
    case ORIENT_UPRIGHT: return "upright";
    case ORIENT_SIDEWAYS: return "sideways";
    case ORIENT_INVERTED: return "inverted";
    default: return "unknown";
  }
}

// cos of the angle between the current gravity vector and the learned upright reference:
// +1 exactly upright, 0 on its side, -1 completely upside down.
static inline float at_orientation_cosine(const Vec3& accel_g, const Vec3& up_ref) {
  Vec3 a = accel_g, r = up_ref;
  if (!v3_normalize(a) || !v3_normalize(r)) return 0.0f;
  return v3_dot(a, r);
}

// Default thresholds leave a wide dead band (60..120 degrees reads as "sideways") so that resting
// the satellite on a side face never looks like a flip.
static inline uint8_t at_classify(const Vec3& accel_g, const Vec3& up_ref, bool up_ref_valid,
                                  float upright_cos = 0.5f, float inverted_cos = -0.5f) {
  if (!up_ref_valid) return ORIENT_UNKNOWN;
  Vec3 a = accel_g;
  if (!v3_normalize(a)) return ORIENT_UNKNOWN;
  float c = at_orientation_cosine(accel_g, up_ref);
  if (c >= upright_cos) return ORIENT_UPRIGHT;
  if (c <= inverted_cos) return ORIENT_INVERTED;
  return ORIENT_SIDEWAYS;
}

// Requires an orientation to persist before it counts, so waving the satellite around on the way
// to the table does not fold the panels. Feed it every telemetry cycle with the time since the
// previous call.
//
// Timing: the call that first *sees* a new orientation starts its clock at zero -- the change
// happened at an unknown moment during that interval, so crediting the whole interval would
// over-count. Time then accumulates on each following call, and the trigger fires once
// `required_s` has genuinely elapsed since the orientation was first seen. In practice that
// means one extra tick of delay, which is the safe direction to err in when the outcome is a
// servo movement.
struct OrientationDebouncer {
  uint8_t candidate;      // orientation currently accumulating time
  uint8_t stable;         // last orientation that held long enough
  float   held_s;         // how long `candidate` has held
  float   required_s;     // how long it must hold

  void reset(float require_seconds) {
    candidate = ORIENT_UNKNOWN; stable = ORIENT_UNKNOWN; held_s = 0.0f; required_s = require_seconds;
  }
  // Returns true on the transition to a *new* stable orientation (the edge the caller acts on).
  bool update(uint8_t observed, float dt_s) {
    if (observed != candidate) { candidate = observed; held_s = 0.0f; return false; }
    held_s += dt_s;
    if (held_s < required_s) return false;
    if (candidate == stable) return false;
    stable = candidate;
    return true;
  }
};
