// Sensors task: sole owner of the I2C bus. Publishes a Telemetry snapshot at 2 Hz and, for the
// IMU only, integrates attitude at a dedicated 200 Hz sub-loop (the stock firmware read the IMU
// in bursts from the 2 Hz loop and lost ~80% of rotation to a clamped dt on the first sample of
// each burst -- this task exists specifically to fix that).
#pragma once
#include <Arduino.h>

void sensors_task_start();          // creates the FreeRTOS task, call once from setup()
void sensors_request_calibration(); // non-blocking: arms a 5 s gyro-bias calibration on the next cycle
bool sensors_calibration_pending();
void sensors_request_gravity_align(); // pins current accel reading as the "level" reference for TRIAD-lite
