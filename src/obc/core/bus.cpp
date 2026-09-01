#include "core/bus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_tlock, s_hlock;
static Telemetry s_tm;
static Housekeeping s_hk;

void bus_init() {
  s_tlock = xSemaphoreCreateMutex();
  s_hlock = xSemaphoreCreateMutex();
  memset(&s_tm, 0, sizeof s_tm);
  memset(&s_hk, 0, sizeof s_hk);
  s_hk.reset_reason = "";
}
void telemetry_publish(const Telemetry& t) { xSemaphoreTake(s_tlock, portMAX_DELAY); s_tm = t; xSemaphoreGive(s_tlock); }
void telemetry_get(Telemetry& out)          { xSemaphoreTake(s_tlock, portMAX_DELAY); out = s_tm; xSemaphoreGive(s_tlock); }
void hk_publish(const Housekeeping& h)      { xSemaphoreTake(s_hlock, portMAX_DELAY); s_hk = h; xSemaphoreGive(s_hlock); }
void hk_get(Housekeeping& out)              { xSemaphoreTake(s_hlock, portMAX_DELAY); out = s_hk; xSemaphoreGive(s_hlock); }
