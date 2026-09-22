// The bench demonstration show: pull the "remove before flight" pin and the satellite performs a
// fixed, repeatable routine for whoever is watching -- the solar wings deploy and fold back twice,
// then the front light (STAR LED) turns on for three seconds, three times.
//
// The show's *schedule* is portable and unit tested in shared/demo_show.h; this is the runner that
// turns each step into a servo command or a light change. It is ticked by the mission task, which
// keeps a single owner for the servo: the mission sequencer holds phase MPHASE_DEMO for the whole
// show, so the orientation triggers cannot fight it for the mechanism.
#pragma once
#include <Arduino.h>
#include "demo_show.h"

void     demo_init();                       // load the schedule from the parameter table
bool     demo_start(const char* reason);    // false if the show is already running or is empty
void     demo_stop(const char* reason);     // stop issuing steps; see the note in demo.cpp
bool     demo_running();
void     demo_tick();                       // call from the mission task
uint8_t  demo_step_kind();                  // current DemoStepKind, DEMO_STEP_NONE when idle
uint32_t demo_remaining_s();                // seconds left in the show, 0 when idle
void     demo_status(Print& out);
void     demo_print_plan(Print& out);       // the schedule the current parameters produce
