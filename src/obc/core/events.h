// On-board event log: compact records queued from any task, persisted by the control tick.
#pragma once
#include <Arduino.h>
#include "mysat_icd.h"

struct Event {
  uint32_t uptime_ms;
  uint32_t epoch;        // unix seconds if the clock is valid, else 0
  uint16_t code;
  int32_t  value;
  char     text[64];
};

void events_init();
void events_post(uint16_t code, int32_t value, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
// Drain queued events to /events.log (call from one task only). Returns number written.
int  events_flush();
// Stream the log file to a Print (serial or HTTP). Returns bytes written.
size_t events_dump(Print& out, int maxLines = 200);
void events_clear();
const char* events_path();
