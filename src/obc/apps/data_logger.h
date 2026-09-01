// Mission data CSV logger. Buffers rows in RAM and flushes in blocks (the stock firmware wrote
// LittleFS on every single row, which is unnecessary flash wear); hourly file rotation and an
// oldest-file-first cap are kept from the original design.
#pragma once
#include <Arduino.h>
#include "core/bus.h"

void logger_init();
void logger_start(uint16_t period_s);
void logger_stop();
void logger_delete_all();
void logger_tick(const Telemetry& tm);     // call once per telemetry publish
bool logger_enabled();
uint16_t logger_period_s();
uint32_t logger_row_count();
void logger_list(Print& out);
String logger_list_json();
bool logger_download(const char* filename, Print& out);
