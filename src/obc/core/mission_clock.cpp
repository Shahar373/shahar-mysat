#include "core/mission_clock.h"
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

static ClockStatus s_status = CLOCK_UNSYNC;

static bool tm_plausible(const struct tm* t) {
  return t && t->tm_year + 1900 >= 2024 && t->tm_year + 1900 <= 2099 && t->tm_mon >= 0 && t->tm_mon < 12 &&
         t->tm_mday >= 1 && t->tm_mday <= 31 && t->tm_hour < 24 && t->tm_min < 60 && t->tm_sec < 61;
}

void clock_set_system(const struct tm* t, ClockStatus how) {
  struct tm c = *t;
  time_t e = mktime(&c);          // TZ is UTC (never set), so this is a UTC conversion
  struct timeval tv = { .tv_sec = e, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  s_status = how;
}

void clock_init_from_rtc(bool rtcPresent, const struct tm* rtcTime) {
  if (rtcPresent && tm_plausible(rtcTime)) clock_set_system(rtcTime, CLOCK_RTC);
  else s_status = CLOCK_UNSYNC;
}

ClockStatus clock_status() { return s_status; }
const char* clock_status_str() {
  switch (s_status) {
    case CLOCK_RTC: return "RTC";
    case CLOCK_SET_BY_CMD: return "CMD";
    case CLOCK_NTP: return "NTP";
    default: return "UNSYNC";
  }
}
bool clock_valid() { return s_status != CLOCK_UNSYNC; }

uint32_t clock_epoch_or_zero() {
  if (!clock_valid()) return 0;
  return (uint32_t)time(nullptr);
}

void clock_format_epoch(uint32_t epoch, char* out, size_t cap) {
  if (epoch == 0) { snprintf(out, cap, "----------T--:--:--Z"); return; }
  time_t e = epoch; struct tm t; gmtime_r(&e, &t);
  strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &t);
}

void clock_now_iso(char* out, size_t cap) { clock_format_epoch(clock_epoch_or_zero(), out, cap); }

bool clock_parse_iso(const char* s, struct tm* out) {
  if (!s || !out) return false;
  int Y, M, D, h, m, sec;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6 &&
      sscanf(s, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return false;
  memset(out, 0, sizeof *out);
  out->tm_year = Y - 1900; out->tm_mon = M - 1; out->tm_mday = D;
  out->tm_hour = h; out->tm_min = m; out->tm_sec = sec;
  return tm_plausible(out);
}

uint32_t clock_met_s(uint32_t launch_epoch) {
  uint32_t now = clock_epoch_or_zero();
  if (!launch_epoch || !now || now < launch_epoch) return 0;
  return now - launch_epoch;
}

void clock_format_duration(uint32_t s, char* out, size_t cap) {
  uint32_t d = s / 86400; s %= 86400;
  snprintf(out, cap, "%02lud %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)(s / 3600),
           (unsigned long)((s % 3600) / 60), (unsigned long)(s % 60));
}
