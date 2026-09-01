#include "core/params_store.h"
#include "core/log.h"
#include "core/events.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

Params g_params;
static SemaphoreHandle_t s_lock;
static const char* NS = "mysat";
static const char* KEY_WORK = "p.work";
static const char* KEY_GOLD = "p.gold";

void params_lock() { if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex(); xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void params_unlock() { xSemaphoreGiveRecursive(s_lock); }

static bool read_slot(const char* key, Params& out) {
  Preferences prefs;
  if (!prefs.begin(NS, true)) return false;
  bool ok = false;
  if (prefs.isKey(key) && prefs.getBytesLength(key) == sizeof(Params)) {
    prefs.getBytes(key, &out, sizeof(Params));
    ok = params_check(out);
  }
  prefs.end();
  return ok;
}

static bool write_slot(const char* key, Params& p) {
  params_seal(p);
  Preferences prefs;
  if (!prefs.begin(NS, false)) return false;
  size_t n = prefs.putBytes(key, &p, sizeof(Params));
  prefs.end();
  return n == sizeof(Params);
}

int params_load() {
  params_lock();
  Params tmp;
  int src;
  if (read_slot(KEY_WORK, tmp)) { g_params = tmp; src = 0; }
  else if (read_slot(KEY_GOLD, tmp)) {
    g_params = tmp; src = 1;
    write_slot(KEY_WORK, g_params);
    events_post(EV_PARAMS_RESTORED, 0, "working params invalid, restored from golden");
  } else {
    params_set_defaults(g_params); src = 2;
    write_slot(KEY_WORK, g_params);
    write_slot(KEY_GOLD, g_params);
    events_post(EV_PARAMS_DEFAULTS, 0, "no valid params, defaults written");
  }
  if (params_sanitize(g_params)) { write_slot(KEY_WORK, g_params); LOGW("PARAMS", "out-of-range values clamped"); }
  params_unlock();
  return src;
}

bool params_save() {
  params_lock();
  params_sanitize(g_params);
  bool ok = write_slot(KEY_WORK, g_params);
  params_unlock();
  if (ok) events_post(EV_PARAMS_SAVED, 0, "params saved");
  else LOGE("PARAMS", "save failed");
  return ok;
}

bool params_commit_golden() {
  params_lock();
  bool ok = write_slot(KEY_WORK, g_params) && write_slot(KEY_GOLD, g_params);
  params_unlock();
  return ok;
}

bool params_restore_golden() {
  params_lock();
  Params tmp; bool ok = read_slot(KEY_GOLD, tmp);
  if (ok) { g_params = tmp; write_slot(KEY_WORK, g_params); }
  params_unlock();
  return ok;
}

void params_factory_reset() {
  params_lock();
  params_set_defaults(g_params);
  write_slot(KEY_WORK, g_params);
  params_unlock();
  events_post(EV_PARAMS_DEFAULTS, 0, "factory reset of working params");
}
