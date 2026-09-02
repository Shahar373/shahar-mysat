// Telemetry snapshot shared between tasks. The sensors task publishes, everyone else copies.
#pragma once
#include <Arduino.h>
#include "mysat_icd.h"
#include <time.h>

struct EnvData {
  bool  valid;
  float temperature_c, humidity_pct, pressure_hpa, gas_kohm, iaq;
  uint8_t iaq_accuracy;
};
struct AttitudeData {
  bool  valid;
  bool  calibrated;
  float roll_deg, pitch_deg, yaw_deg;        // zero-offset applied
  float rate_dps[3];                         // body rates
  float rate_mag_dps;                        // |omega|
  float accel_g[3];
  float imu_temp_c;
  uint8_t imu_whoami;                        // 0x71 MPU9250, 0x70 MPU6500, 0x68 MPU6050, 0 unknown
  uint32_t samples;                          // total IMU samples integrated
};
struct SunData {
  bool  valid;
  int16_t raw[4];                            // ph1 left, ph2 back, ph3 right, ph4 front (ADC counts)
  float volts[4];
};
struct PowerData {
  bool  valid;
  float batt_v, batt_ma;
  float panel_v, panel_left_ma, panel_right_ma;
};
struct RtcData {
  bool  valid;
  struct tm t;
  float temp_c;
};

struct Telemetry {
  uint32_t seq;
  uint32_t uptime_ms;
  EnvData env;
  AttitudeData att;
  SunData sun;
  PowerData pwr;
  RtcData rtc;
};

struct Housekeeping {
  uint32_t uptime_s;
  uint32_t boots;
  const char* reset_reason;
  uint32_t heap_free, heap_min, psram_free;
  float    cpu_temp_c;
  uint32_t fs_used, fs_total;
  uint8_t  mode;
  uint8_t  mission_phase;     // MissionPhase from the sequencer
  uint32_t mission_countdown_s;
  uint8_t  orientation;       // Orientation from attitude_trigger.h
  uint8_t  wifi_mode;         // 0 off 1 sta 2 ap
  bool     wifi_connected;
  int8_t   wifi_rssi;
  char     ip[16];
  bool     aux_ok;
  AuxStatus aux;
  bool     panels_deployed;
  bool     star_led;
  bool     camera_ok;
  uint32_t i2c_errors[7];
};

void bus_init();
void telemetry_publish(const Telemetry& t);
void telemetry_get(Telemetry& out);
void hk_publish(const Housekeeping& h);
void hk_get(Housekeeping& out);
