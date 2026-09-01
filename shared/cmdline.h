// One-line command tokenizer shared by the serial console, the web /api/cmd endpoint and host tests.
// Splits in place on spaces/tabs, honours "double quotes" so SSIDs and passwords may contain spaces.
#pragma once
#include <stddef.h>
#include <string.h>

#define CMDLINE_MAX_ARGS 10
#define CMDLINE_MAX_LEN  200

static inline int cmdline_tokenize(char* s, char** argv, int maxArgs) {
  int argc = 0;
  while (*s && argc < maxArgs) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (!*s) break;
    if (*s == '"') {
      s++;
      argv[argc++] = s;
      while (*s && *s != '"') s++;
      if (*s) *s++ = 0;
    } else {
      argv[argc++] = s;
      while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') s++;
      if (*s) *s++ = 0;
    }
  }
  return argc;
}

static inline void str_tolower_inplace(char* s) {
  for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32;
}

// Legacy v1.x command names mapped onto the v2 grammar so old habits (and old docs) still work.
struct LegacyAlias { const char* legacy; const char* modern; };
static const LegacyAlias LEGACY_ALIASES[] = {
  { "solardeploy",  "solar deploy" },
  { "solarretract", "solar retract" },
  { "solarmove",    "solar toggle" },
  { "turnled",      "led toggle" },
  { "blinkled",     "led blink" },
  { "calibrate",    "imu calibrate" },
  { "turnconsole",  "console toggle" },
  { "switchtelemetry", "console plotter" },
  { "debugmodeon",  "loglevel 3" },
  { "debugmodeoff", "loglevel 2" },
  { "startlogging", "log start" },
  { "stoplogging",  "log stop" },
  { "deletelogging","log delete" },
  { "listlogfiles", "log list" },
  { "auditfilesystem", "fs" },
  { "sendeventlog", "events" },
  { "setradio",     "radio at" },
};
#define LEGACY_ALIASES_LEN (sizeof(LEGACY_ALIASES) / sizeof(LEGACY_ALIASES[0]))

static inline bool cmdline_ieq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return false;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

// If the whole line is a legacy single word, rewrite it to the modern form (in place, buffer must hold cap bytes).
static inline bool cmdline_apply_legacy_alias(char* line, size_t cap) {
  char tmp[CMDLINE_MAX_LEN];
  strncpy(tmp, line, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
  char* p = tmp; while (*p == ' ') p++;
  char* e = p + strlen(p); while (e > p && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  if (strchr(p, ' ')) return false;
  for (size_t i = 0; i < LEGACY_ALIASES_LEN; i++) {
    if (cmdline_ieq(p, LEGACY_ALIASES[i].legacy)) {
      strncpy(line, LEGACY_ALIASES[i].modern, cap - 1); line[cap - 1] = 0;
      return true;
    }
  }
  return false;
}
