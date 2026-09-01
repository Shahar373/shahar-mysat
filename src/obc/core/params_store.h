// NVS-backed storage for the CRC-protected parameter table (working + golden copies).
#pragma once
#include <Arduino.h>
#include "params_def.h"

extern Params g_params;      // live table; take params_lock() for multi-field consistency

void params_lock();
void params_unlock();
// Load working copy; fall back to golden, then to defaults. Returns source: 0 work, 1 golden, 2 defaults.
int  params_load();
bool params_save();          // seal + write working copy
bool params_commit_golden(); // copy the current working table into the golden slot
bool params_restore_golden();// overwrite working with golden (if valid)
void params_factory_reset(); // defaults + save (does not touch golden)
