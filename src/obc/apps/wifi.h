// WiFi manager: station-with-AP-fallback, or AP-only ("standalone demo" mode with zero setup).
// Runs in its own FreeRTOS task so a slow/failing station connect never blocks sensors, the
// console or the AUX link (the stock firmware's tryConnectWiFi() ran in the middle of setup()).
#pragma once
#include <Arduino.h>

void wifi_task_start();
void wifi_apply_params();     // request a reconnect using the current g_params (called after 'wifi apply')
bool wifi_get_status(uint8_t& mode, bool& connected, int8_t& rssi, char* ip, size_t ipLen);
