// MySat Interface Control Document (ICD) — values shared by the OBC (ESP32-CAM),
// the AUX controller (Arduino Nano) and ground tools. Keep this header C++11 and
// free of Arduino types so it compiles on AVR, ESP32 and the host.
#pragma once
#include <stdint.h>

// ------------------------------------------------------------------ AUX (Nano) I2C link
#define MYSAT_AUX_I2C_ADDR   0x08
#define AUX_PROTO_VERSION    2

// v1.x compatible single-byte commands. A 1-byte write is always interpreted this way.
enum AuxLegacyCmd : uint8_t {
  AUX_LEGACY_MOTOR_OPEN  = 0,
  AUX_LEGACY_MOTOR_CLOSE = 1,
  AUX_LEGACY_RF_TURN     = 2,   // no-op in v1.x, kept for compatibility
  AUX_LEGACY_RF_SET      = 3,   // v2: enters HC-12 AT mode for 60 s (auto exit)
};

// v2 framed command: [AUX_FRAME_MAGIC][cmd][arg][crc8_smbus over the first 3 bytes]
#define AUX_FRAME_MAGIC 0xA5
#define AUX_FRAME_LEN   4

enum AuxCmd : uint8_t {
  AUX_CMD_NOP          = 0x00,
  AUX_CMD_MOTOR_OPEN   = 0x01,  // deploy wings (servo to open angle)
  AUX_CMD_MOTOR_CLOSE  = 0x02,  // retract wings (servo to closed angle)
  AUX_CMD_SERVO_ANGLE  = 0x10,  // arg = target angle (clamped to the mechanical range)
  AUX_CMD_RF_SET       = 0x11,  // arg 1 = HC-12 AT mode (SET pin low), 0 = transparent mode
  AUX_CMD_RF_POWER     = 0x12,  // arg 1 = radio powered, 0 = radio off
  AUX_CMD_HEARTBEAT    = 0x20,  // arg = OBC mode id; AUX tracks the age of the last heartbeat
  AUX_CMD_LED          = 0x30,  // arg 0 = off, 1 = on, 2 = default pattern
  AUX_CMD_RESET_STATS  = 0x7F,  // clear command / crc counters
};

// Servo timing, shared so the OBC can pace its commands to what the AUX actually does.
// The AUX powers the servo for AUX_SERVO_POWER_MS per movement and refuses to start a new
// movement while one is running, so a command sent sooner than AUX_SERVO_MIN_CMD_GAP_MS after the
// previous one only retargets the sweep in progress -- it does not produce a second sweep.
#define AUX_SERVO_STEP_MS         10    // ms per degree while sweeping
#define AUX_SERVO_POWER_MS        2200  // servo power window per movement (stall protection)
#define AUX_SERVO_MIN_CMD_GAP_MS  2400  // power window plus one AUX loop pass to detach

// Servo state as reported by AUX
enum AuxServoState : uint8_t {
  AUX_SERVO_OFF = 0,      // detached, idle
  AUX_SERVO_TURNING = 1,  // powered and stepping toward the target
  AUX_SERVO_DONE = 2,     // reached target or power-cutoff, about to detach
};

// Status frame returned to the OBC on an I2C read (Wire.requestFrom). 16 bytes, crc8 last.
#define AUX_STATUS_MAGIC 0x5A
struct __attribute__((packed)) AuxStatus {
  uint8_t  magic;        // AUX_STATUS_MAGIC
  uint8_t  proto;        // AUX_PROTO_VERSION
  uint8_t  fw;           // (major << 4) | minor
  uint32_t uptime_s;
  uint8_t  servo_angle;  // current commanded angle, degrees
  uint8_t  servo_state;  // AuxServoState
  uint8_t  rf_flags;     // bit0 = SET pin asserted (AT mode), bit1 = radio powered
  uint16_t hb_age_s;     // seconds since last OBC heartbeat, 0xFFFF = never received
  uint8_t  cmd_count;    // commands accepted (mod 256)
  uint8_t  crc_errors;   // framed commands rejected (mod 256)
  uint8_t  boot_flags;   // bit0 = last AUX reset was its watchdog, bit1 = brown-out
  uint8_t  crc8;         // crc8_smbus over the 15 preceding bytes
};
#define AUX_STATUS_LEN 16

// ------------------------------------------------------------------ OBC identifiers
// Mission modes (phase 1 will add transitions; phase 0 only reports NOMINAL/SAFE)
enum MissionMode : uint8_t {
  MODE_BOOT = 0,
  MODE_SAFE = 1,
  MODE_NOMINAL = 2,
  MODE_LEOP = 3,
  MODE_COMMISSIONING = 4,
  MODE_SCIENCE = 5,
  MODE_LOW_POWER = 6,
};

// Event codes (stored compactly on board, rendered as text on the ground)
enum EventCode : uint16_t {
  EV_BOOT = 1,
  EV_SHUTDOWN_RECONSTRUCTED = 2,
  EV_RESET_REASON = 3,
  EV_SENSOR_FAIL = 10,
  EV_SENSOR_RECOVERED = 11,
  EV_I2C_BUS_RECOVERY = 12,
  EV_PARAMS_RESTORED = 20,
  EV_PARAMS_DEFAULTS = 21,
  EV_PARAMS_SAVED = 22,
  EV_CMD = 30,
  EV_CMD_REJECTED = 31,
  EV_PHOTO = 40,
  EV_MOTOR = 50,
  EV_WIFI = 60,
  EV_MODE = 70,
  EV_CLOCK = 80,
  EV_LOGGER = 90,
  EV_AUX = 100,
  EV_WDT = 110,
  EV_LOG_WARN = 120,
  EV_LOG_ERROR = 121,
  // mission sequencer
  EV_SEPARATION = 130,          // the launch pin was pulled (power-on) or a manual separation
  EV_DEPLOY_START = 131,
  EV_DEPLOY_CONFIRMED = 132,    // AUX reported the servo reached the open angle
  EV_DEPLOY_UNCONFIRMED = 133,  // command sent, but AUX could not confirm it
  EV_STOW = 134,                // panels retracted (turned upside down, or commanded)
  EV_ORIENTATION = 135,
  EV_UPREF_LEARNED = 136,       // "this way up" reference vector captured
  EV_PHASE = 137,
  // bench demonstration show (apps/demo.cpp)
  EV_DEMO = 140,
};

// Mission sequencer phases (apps/mission.cpp)
enum MissionPhase : uint8_t {
  MPHASE_BOOT = 0,       // subsystems still coming up
  MPHASE_PRELAUNCH = 1,  // powered, but no separation seen (e.g. after a software reboot)
  MPHASE_LEOP = 2,       // separation detected, counting down to deployment
  MPHASE_DEPLOYING = 3,  // servo commanded, waiting for confirmation
  MPHASE_NOMINAL = 4,    // panels deployed
  MPHASE_STOWED = 5,     // panels retracted after being turned upside down
  MPHASE_MANUAL = 6,     // operator drove the panels by hand; automatic triggers suspended
  MPHASE_DEMO = 7,       // running the bench demonstration show (shared/demo_show.h)
};
