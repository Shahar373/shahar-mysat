// Mission clock: DS3231 <-> ESP32 system time, clock status and mission elapsed time (MET).
// The RTC is read once at boot (and on command); everything else uses the system clock so
// files, events, scheduler and telemetry agree. An invalid RTC is a telemetry flag, never a prompt.
#pragma once
#include <Arduino.h>
#include <time.h>

enum ClockStatus : uint8_t { CLOCK_UNSYNC = 0, CLOCK_RTC = 1, CLOCK_SET_BY_CMD = 2, CLOCK_NTP = 3 };

void        clock_init_from_rtc(bool rtcPresent, const struct tm* rtcTime);  // called by the sensors task after RTC read
ClockStatus clock_status();
const char* clock_status_str();
bool        clock_valid();
uint32_t    clock_epoch_or_zero();
void        clock_now_iso(char* out, size_t cap);                 // "2026-09-01T12:00:00Z" or "----"
void        clock_format_epoch(uint32_t epoch, char* out, size_t cap);
// Parse "YYYY-MM-DDTHH:MM:SS" (UTC). Returns false on a bad string.
bool        clock_parse_iso(const char* s, struct tm* out);
void        clock_set_system(const struct tm* t, ClockStatus how);
// Mission elapsed time in seconds since launch_epoch (0 if not launched or clock unknown)
uint32_t    clock_met_s(uint32_t launch_epoch);
void        clock_format_duration(uint32_t s, char* out, size_t cap);   // "02d 14:07:33"
