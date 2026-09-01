#include "core/log.h"
#include "core/events.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>

static SemaphoreHandle_t s_lock;
static uint8_t s_level = LOG_INFO;

void log_init() {
  s_lock = xSemaphoreCreateRecursiveMutex();
}
void log_set_level(uint8_t level) { s_level = level > LOG_DEBUG ? LOG_DEBUG : level; }
uint8_t log_level() { return s_level; }

void console_lock() { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void console_unlock() { if (s_lock) xSemaphoreGiveRecursive(s_lock); }

void log_printf(uint8_t level, const char* tag, const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);

  if (level <= s_level) {
    static const char* names[] = {"ERROR", "WARN ", "INFO ", "DEBUG"};
    console_lock();
    Serial.printf("[%8lu][%s][%s] %s\n", (unsigned long)millis(), names[level], tag, buf);
    console_unlock();
  }
  if (level <= LOG_WARN) {
    events_post(level == LOG_ERROR ? EV_LOG_ERROR : EV_LOG_WARN, 0, "%s: %s", tag, buf);
  }
}
