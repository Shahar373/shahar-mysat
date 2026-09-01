#include "apps/wifi.h"
#include "core/params_store.h"
#include "core/log.h"
#include "core/events.h"
#include "core/fdir.h"
#include "hal/leds.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_applySem;
static uint8_t s_modeActive = 0;
static bool s_connected = false;
static char s_ip[16] = "0.0.0.0";

static void start_ap() {
  char ssid[24];
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(ssid, sizeof ssid, "%s-%02X%02X", g_params.callsign, mac[4], mac[5]);
  WiFi.mode(WIFI_AP);
  bool ok = g_params.ap_pass[0] ? WiFi.softAP(ssid, g_params.ap_pass) : WiFi.softAP(ssid);
  IPAddress ip = WiFi.softAPIP();
  snprintf(s_ip, sizeof s_ip, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  s_modeActive = 2; s_connected = ok;
  leds_set(LED_AP_MODE);
  LOGI("WIFI", "access point '%s' %s, join it and browse to %s", ssid, ok ? "up" : "FAILED", s_ip);
  events_post(EV_WIFI, ok, "AP mode: %s (%s)", ssid, s_ip);
}

static bool try_station() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_params.wifi_ssid, g_params.wifi_pass[0] ? g_params.wifi_pass : nullptr);
  LOGI("WIFI", "connecting to '%s' (timeout %us)...", g_params.wifi_ssid, g_params.sta_timeout_s);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < (uint32_t)g_params.sta_timeout_s * 1000UL) {
    leds_set(LED_NO_LINK);
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    snprintf(s_ip, sizeof s_ip, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    s_modeActive = 1; s_connected = true;
    LOGI("WIFI", "connected, ip %s", s_ip);
    events_post(EV_WIFI, 1, "station connected: %s", s_ip);
    return true;
  }
  LOGW("WIFI", "station connect timed out, falling back to access point");
  events_post(EV_WIFI, 0, "station timeout, falling back to AP");
  return false;
}

static void apply_now() {
  WiFi.disconnect(true, true);
  s_connected = false;
  if (g_params.wifi_mode == 0) { WiFi.mode(WIFI_OFF); s_modeActive = 0; strcpy(s_ip, "0.0.0.0"); LOGI("WIFI", "off"); return; }
  if (g_params.wifi_mode == 1) { if (!try_station()) start_ap(); return; }
  start_ap();
}

static void wifi_task(void*) {
  fdir_wdt_subscribe();
  apply_now();
  for (;;) {
    if (xSemaphoreTake(s_applySem, pdMS_TO_TICKS(2000)) == pdTRUE) apply_now();
    if (s_modeActive == 1) {
      bool nowConnected = WiFi.status() == WL_CONNECTED;
      if (nowConnected != s_connected) {
        s_connected = nowConnected;
        events_post(EV_WIFI, nowConnected, nowConnected ? "station reconnected" : "station link lost");
        leds_set(nowConnected ? LED_NOMINAL : LED_NO_LINK);
      }
    }
    fdir_wdt_feed();
  }
}

void wifi_task_start() {
  s_applySem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(wifi_task, "wifi", 4096, nullptr, 2, nullptr, 0);
}
void wifi_apply_params() { if (s_applySem) xSemaphoreGive(s_applySem); }

bool wifi_get_status(uint8_t& mode, bool& connected, int8_t& rssi, char* ip, size_t ipLen) {
  mode = s_modeActive; connected = s_connected;
  rssi = (s_modeActive == 1 && s_connected) ? (int8_t)WiFi.RSSI() : 0;
  strncpy(ip, s_ip, ipLen - 1); ip[ipLen - 1] = 0;
  return true;
}
