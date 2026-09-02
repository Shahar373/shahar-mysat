#include "hal/leds.h"
#include <Adafruit_NeoPixel.h>

#define SIGNAL_LED_PIN 2
#define STAR_LED_PIN   14
#define STAR_PWM_CH    2       // channels 0/1 are used by the camera driver (LEDC_CHANNEL_0/timer 0)
#define STAR_BRIGHTNESS 65
#define SIGNAL_BRIGHTNESS 24

static Adafruit_NeoPixel s_px(1, SIGNAL_LED_PIN, NEO_GRB + NEO_KHZ800);
static LedState s_state = LED_BOOT, s_prev = LED_BOOT;
static uint32_t s_activity_until = 0;
static bool s_star = false;
static uint8_t s_blink_step = 0; static uint32_t s_blink_t = 0; static bool s_blinking = false;

void leds_init() {
  s_px.begin(); s_px.setBrightness(SIGNAL_BRIGHTNESS); s_px.clear(); s_px.show();
  ledcSetup(STAR_PWM_CH, 5000, 8);
  ledcAttachPin(STAR_LED_PIN, STAR_PWM_CH);
  ledcWrite(STAR_PWM_CH, 0);
}

void leds_set(LedState s) { if (s != LED_ACTIVITY) { s_state = s; } }
void leds_flash_activity() { s_activity_until = millis() + 120; }

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return s_px.Color(r, g, b); }

void leds_tick() {
  uint32_t now = millis();
  uint32_t c = 0;
  if (now < s_activity_until) { c = rgb(0, 200, 255); }
  else switch (s_state) {
    case LED_BOOT: { float ph = (now % 1500) / 1500.0f; uint8_t v = (uint8_t)(40 + 200 * fabsf(sinf(ph * 3.14159f))); c = rgb(v, v, v); break; }
    case LED_NOMINAL: { uint32_t t = now % 2000; c = (t < 80 || (t > 200 && t < 280)) ? rgb(0, 255, 60) : rgb(0, 25, 6); break; }  // heartbeat
    case LED_NO_LINK: c = ((now % 1000) < 800) ? rgb(0, 0, 255) : 0; break;
    case LED_AP_MODE: c = rgb(255, 140, 0); break;
    case LED_FAULT: { uint32_t t = now % 1500; c = (t < 100 || (t > 250 && t < 350)) ? rgb(255, 0, 0) : 0; break; }
    case LED_SAFE: { float ph = (now % 3000) / 3000.0f; uint8_t v = (uint8_t)(20 + 200 * fabsf(sinf(ph * 3.14159f))); c = rgb(v, 0, 0); break; }
    case LED_LEOP: { float ph = (now % 1200) / 1200.0f; uint8_t v = (uint8_t)(30 + 220 * fabsf(sinf(ph * 3.14159f))); c = rgb(v, (uint8_t)(v * 0.55f), 0); break; }
    case LED_DEPLOY: c = ((now % 250) < 125) ? rgb(255, 150, 0) : 0; break;
    case LED_STOWED: { uint32_t t = now % 2000; c = (t < 150) ? rgb(200, 0, 200) : rgb(20, 0, 20); break; }
    default: break;
  }
  s_px.setPixelColor(0, c); s_px.show();
}

void star_led_set(bool on) { s_star = on; if (!s_blinking) ledcWrite(STAR_PWM_CH, on ? STAR_BRIGHTNESS : 0); }
bool star_led_get() { return s_star; }
void star_led_blink_test() { s_blinking = true; s_blink_step = 0; s_blink_t = millis(); }
void star_led_tick() {
  if (!s_blinking) return;
  if (millis() - s_blink_t < 250) return;
  s_blink_t = millis();
  if (s_blink_step >= 6) { s_blinking = false; ledcWrite(STAR_PWM_CH, s_star ? STAR_BRIGHTNESS : 0); return; }
  ledcWrite(STAR_PWM_CH, (s_blink_step % 2 == 0) ? STAR_BRIGHTNESS : 0);
  s_blink_step++;
}
