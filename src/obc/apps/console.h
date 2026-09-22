// One-line command interpreter, shared by the serial console and the web /api/cmd endpoint.
// Every command returns its result as text (never blocks on Serial.available()); commands that
// used to prompt interactively (ChangeTime, SetWIFI, SetCallSign...) now take their argument
// inline: "time set 2026-09-01T12:00:00", "wifi ssid MyNetwork", "callsign MYSAT-1".
#pragma once
#include <Arduino.h>

void console_init();
// Executes one command line, writes human-readable output to `out`. Returns true if the command
// was recognized (even if it failed), false for "unknown command".
bool console_execute(const String& line, Print& out);
void console_task_start();          // reads Serial non-blockingly and prints periodic telemetry frames
void console_print_help(Print& out);
void console_print_telemetry_frame(Print& out);
// console_lock()/console_unlock(), which callers take around multi-line output, are declared in
// core/log.h -- they guard the same Serial mutex the log lines use.
