#include "apps/data_logger.h"
#include "core/params_store.h"
#include "core/log.h"
#include "core/events.h"
#include "core/mission_clock.h"
#include <LittleFS.h>

static const uint32_t MAX_TOTAL_ROWS = 20000;      // ~4.5 days at 20 s period before oldest-file rotation
static const uint32_t HOUR_MS = 3600000UL;
static const int FLUSH_ROWS = 20;                  // buffer this many rows before a flash write

static String s_buffer;
static int s_buffered_rows = 0;
static String s_current_file;
static uint32_t s_session_start_ms = 0;
static uint32_t s_total_rows = 0;
static int s_file_number = 0;
static uint32_t s_last_log_ms = 0;

static const char* STATE_FILE = "/logger_state.txt";

static void save_state() {
  File f = LittleFS.open(STATE_FILE, "w");
  if (!f) return;
  f.printf("%d\n%u\n%u\n%s\n%d\n", g_params.logging_enabled, g_params.log_period_s, s_total_rows,
           s_current_file.c_str(), s_file_number);
  f.close();
}

static bool is_log_file(const String& name) { return name.startsWith("mdata_") && name.endsWith(".csv"); }
static String norm(const String& n) { return n.startsWith("/") ? n : "/" + n; }

static uint32_t count_rows(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  uint32_t n = 0; while (f.available()) { f.readStringUntil('\n'); n++; }
  f.close();
  return n > 0 ? n - 1 : 0;   // minus the header row
}

static bool delete_oldest() {
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  String oldest;
  while (file) {
    String name = file.name();
    if (is_log_file(name) && (oldest.isEmpty() || name < oldest)) oldest = name;
    file = root.openNextFile();
  }
  if (oldest.isEmpty()) return false;
  String path = norm(oldest);
  uint32_t rows = count_rows(path);
  LittleFS.remove(path);
  if (rows <= s_total_rows) s_total_rows -= rows; else s_total_rows = 0;
  events_post(EV_LOGGER, rows, "deleted oldest log %s (%u rows freed)", oldest.c_str(), rows);
  return true;
}

static bool new_file() {
  s_file_number++;
  char ts[24]; clock_now_iso(ts, sizeof ts);
  char name[48];
  if (clock_valid()) snprintf(name, sizeof name, "/mdata_%04d_%.4s%.2s%.2sT%.2s%.2s%.2s.csv",
                               s_file_number, ts, ts + 5, ts + 8, ts + 11, ts + 14, ts + 17);
  else snprintf(name, sizeof name, "/mdata_%04d_nortc_%010lu.csv", s_file_number, (unsigned long)millis());
  s_current_file = name;
  s_session_start_ms = millis();
  File f = LittleFS.open(s_current_file, "w");
  if (!f) { s_file_number--; LOGE("LOGGER", "failed to create %s", name); return false; }
  f.print("epoch_utc,uptime_s,temp_c,pressure_hpa,humidity_pct,gas_kohm,iaq,iaq_acc,"
          "roll_deg,pitch_deg,yaw_deg,ph1,ph2,ph3,ph4,batt_v,batt_ma,panel_v,panel_l_ma,panel_r_ma\n");
  f.close();
  save_state();
  return true;
}

void logger_init() {
  if (!LittleFS.exists(STATE_FILE)) return;
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  bool en = f.readStringUntil('\n').toInt() == 1;
  uint16_t period = f.readStringUntil('\n').toInt();
  s_total_rows = f.readStringUntil('\n').toInt();
  s_current_file = f.readStringUntil('\n'); s_current_file.trim();
  s_file_number = f.readStringUntil('\n').toInt();
  f.close();
  params_lock(); g_params.logging_enabled = en; g_params.log_period_s = period ? period : g_params.log_period_s; params_unlock();
  if (en && s_current_file.length() && LittleFS.exists(s_current_file)) {
    LOGI("LOGGER", "resuming %s after reboot (%u rows so far)", s_current_file.c_str(), s_total_rows);
    s_session_start_ms = millis();
  } else if (en) {
    new_file();
  }
}

void logger_start(uint16_t period_s) {
  params_lock(); g_params.logging_enabled = 1; g_params.log_period_s = period_s < 1 ? 1 : period_s; params_unlock();
  params_save();
  s_last_log_ms = millis();
  if (!new_file()) { params_lock(); g_params.logging_enabled = 0; params_unlock(); params_save(); return; }
  events_post(EV_LOGGER, period_s, "logging started, period %us", period_s);
}
void logger_stop() {
  params_lock(); g_params.logging_enabled = 0; params_unlock(); params_save();
  if (s_buffered_rows) { File f = LittleFS.open(s_current_file, "a"); if (f) { f.print(s_buffer); f.close(); } s_buffer = ""; s_buffered_rows = 0; }
  events_post(EV_LOGGER, s_total_rows, "logging stopped, %u rows total", s_total_rows);
}
void logger_delete_all() {
  logger_stop();
  File root = LittleFS.open("/"); File file = root.openNextFile();
  int n = 0;
  while (file) { String name = file.name(); file = root.openNextFile(); if (is_log_file(name)) { LittleFS.remove(norm(name)); n++; } }
  s_total_rows = 0; s_file_number = 0; s_current_file = "";
  save_state();
  events_post(EV_LOGGER, n, "deleted all %d log files", n);
}
bool logger_enabled() { return g_params.logging_enabled; }
uint16_t logger_period_s() { return g_params.log_period_s; }
uint32_t logger_row_count() { return s_total_rows; }

void logger_tick(const Telemetry& tm) {
  if (!g_params.logging_enabled) return;
  if (millis() - s_session_start_ms >= HOUR_MS) new_file();
  if (millis() - s_last_log_ms < (uint32_t)g_params.log_period_s * 1000UL) return;
  s_last_log_ms = millis();

  if (s_total_rows >= MAX_TOTAL_ROWS) { if (!delete_oldest()) { logger_stop(); return; } }

  char line[220];
  snprintf(line, sizeof line,
    "%lu,%.3f,"
    "%.2f,%.2f,%.2f,%.2f,%.0f,%u,"
    "%.1f,%.1f,%.1f,"
    "%d,%d,%d,%d,"
    "%.3f,%.2f,%.3f,%.2f,%.2f\n",
    (unsigned long)clock_epoch_or_zero(), tm.uptime_ms / 1000.0,
    tm.env.valid ? tm.env.temperature_c : NAN, tm.env.valid ? tm.env.pressure_hpa : NAN,
    tm.env.valid ? tm.env.humidity_pct : NAN, tm.env.valid ? tm.env.gas_kohm : NAN,
    tm.env.valid ? tm.env.iaq : NAN, tm.env.iaq_accuracy,
    tm.att.roll_deg, tm.att.pitch_deg, tm.att.yaw_deg,
    tm.sun.raw[0], tm.sun.raw[1], tm.sun.raw[2], tm.sun.raw[3],
    tm.pwr.batt_v, tm.pwr.batt_ma, tm.pwr.panel_v, tm.pwr.panel_left_ma, tm.pwr.panel_right_ma);

  s_buffer += line; s_buffered_rows++; s_total_rows++;
  if (s_buffered_rows >= FLUSH_ROWS) {
    File f = LittleFS.open(s_current_file, "a");
    if (f) { f.print(s_buffer); f.close(); } else LOGE("LOGGER", "write failed: %s", s_current_file.c_str());
    s_buffer = ""; s_buffered_rows = 0;
    save_state();
  }
}

void logger_list(Print& out) {
  File root = LittleFS.open("/"); File file = root.openNextFile();
  int n = 0;
  while (file) {
    String name = file.name();
    if (is_log_file(name)) { out.printf("%-40s %8u bytes\n", name.c_str(), (unsigned)file.size()); n++; }
    file = root.openNextFile();
  }
  out.printf("%d file(s), %u rows total\n", n, (unsigned)s_total_rows);
}

String logger_list_json() {
  String j = "[";
  File root = LittleFS.open("/"); File file = root.openNextFile();
  bool first = true;
  while (file) {
    String name = file.name();
    if (is_log_file(name)) {
      if (!first) j += ","; first = false;
      j += "{\"name\":\""; j += name; j += "\",\"size\":"; j += file.size(); j += "}";
    }
    file = root.openNextFile();
  }
  j += "]";
  return j;
}

bool logger_download(const char* filename, Print& out) {
  String path = norm(String(filename));
  if (!is_log_file(path.substring(1)) || !LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  uint8_t buf[256]; int n;
  while ((n = f.read(buf, sizeof buf)) > 0) out.write(buf, n);
  f.close();
  return true;
}
