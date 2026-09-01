// Serial logging with levels. Every task prints through one mutex so frames and log lines
// never interleave mid-line. WARN and ERROR are also recorded as on-board events.
#pragma once
#include <Arduino.h>

enum LogLevel : uint8_t { LOG_ERROR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DEBUG = 3 };

void log_init();
void log_set_level(uint8_t level);
uint8_t log_level();
void log_printf(uint8_t level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

// Exclusive access to Serial for multi-line output (telemetry frames, listings).
void console_lock();
void console_unlock();

#define LOGE(tag, ...) log_printf(LOG_ERROR, tag, __VA_ARGS__)
#define LOGW(tag, ...) log_printf(LOG_WARN, tag, __VA_ARGS__)
#define LOGI(tag, ...) log_printf(LOG_INFO, tag, __VA_ARGS__)
#define LOGD(tag, ...) log_printf(LOG_DEBUG, tag, __VA_ARGS__)
