// HTTP API + web UI. Serves data/index.html from LittleFS if it was uploaded (pio run -t
// uploadfs); otherwise falls back to a compact inline page so the satellite is always usable
// standalone, with zero PC tooling, over its own access point.
#pragma once
#include <Arduino.h>

void web_task_start();
