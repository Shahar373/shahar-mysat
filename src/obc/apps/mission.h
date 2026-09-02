// Mission sequencer: the launch-and-deployment logic.
//
//   pull the launch pin  ->  the satellite powers up  ->  it recognises that as SEPARATION
//   ->  LEOP countdown   ->  panels deploy automatically  ->  deployment is confirmed from
//   telemetry, not assumed  ->  NOMINAL.
//
// After that it watches its own orientation: turned upside down, it stows the panels (a real
// spacecraft folds appendages when something is wrong); turned back upright, it deploys again.
// A manual `solar ...` command suspends the automatic triggers so the servo never fights the
// operator -- `mission auto on` re-arms them.
#pragma once
#include <Arduino.h>
#include "mysat_icd.h"

void        mission_task_start();
uint8_t     mission_phase();
const char* mission_phase_str();
uint32_t    mission_countdown_s();        // seconds left in the LEOP countdown, 0 when not counting
uint8_t     mission_orientation();        // last debounced Orientation value
bool        mission_auto_enabled();

void mission_trigger_separation();        // manual "pull the pin" for demos, no power cycle needed
void mission_abort();                     // cancel a pending deployment, hold position
void mission_learn_upright();             // record the current attitude as "this way up"
void mission_set_auto(bool on);
void mission_note_manual_actuation();     // called by the console when a human drives the panels
void mission_status(Print& out);
