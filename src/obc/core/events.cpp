#include "core/events.h"
#include "core/mission_clock.h"
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdarg.h>

static QueueHandle_t s_q;
static const char* EVENTS_FILE = "/events.log";
static const size_t EVENTS_MAX_BYTES = 24 * 1024;   // rotate: keep the newer half

void events_init() {
  s_q = xQueueCreate(32, sizeof(Event));
}

const char* events_path() { return EVENTS_FILE; }

void events_post(uint16_t code, int32_t value, const char* fmt, ...) {
  if (!s_q) return;
  Event e;
  e.uptime_ms = millis();
  e.epoch = clock_epoch_or_zero();
  e.code = code;
  e.value = value;
  va_list ap; va_start(ap, fmt);
  vsnprintf(e.text, sizeof e.text, fmt, ap);
  va_end(ap);
  xQueueSend(s_q, &e, 0);   // drop if full; never block the producer
}

static void rotate_if_needed() {
  File f = LittleFS.open(EVENTS_FILE, "r");
  if (!f) return;
  size_t sz = f.size();
  if (sz < EVENTS_MAX_BYTES) { f.close(); return; }
  f.seek(sz / 2);
  f.readStringUntil('\n');   // align to a line boundary
  File t = LittleFS.open("/events.tmp", "w");
  if (!t) { f.close(); return; }
  uint8_t buf[256];
  while (f.available()) { int n = f.read(buf, sizeof buf); if (n > 0) t.write(buf, n); }
  f.close(); t.close();
  LittleFS.remove(EVENTS_FILE);
  LittleFS.rename("/events.tmp", EVENTS_FILE);
}

int events_flush() {
  if (!s_q || uxQueueMessagesWaiting(s_q) == 0) return 0;
  rotate_if_needed();
  File f = LittleFS.open(EVENTS_FILE, "a");
  int n = 0;
  Event e;
  while (xQueueReceive(s_q, &e, 0) == pdTRUE) {
    char ts[24];
    clock_format_epoch(e.epoch, ts, sizeof ts);   // "----" when unknown
    if (f) f.printf("%s %10lu %3u %ld %s\n", ts, (unsigned long)e.uptime_ms, (unsigned)e.code, (long)e.value, e.text);
    n++;
  }
  if (f) f.close();
  return n;
}

size_t events_dump(Print& out, int maxLines) {
  File f = LittleFS.open(EVENTS_FILE, "r");
  if (!f) return 0;
  // count lines to skip the oldest when the file has more than maxLines
  int lines = 0;
  while (f.available()) { if (f.read() == '\n') lines++; }
  int skip = lines > maxLines ? lines - maxLines : 0;
  f.seek(0);
  size_t written = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (skip > 0) { skip--; continue; }
    written += out.println(l);
  }
  f.close();
  return written;
}

void events_clear() { LittleFS.remove(EVENTS_FILE); }
