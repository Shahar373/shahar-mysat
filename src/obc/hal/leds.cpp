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
static uint8_t  s_level = 0;                             // duty actually on the pin right now
static uint8_t  s_fade_from = 0, s_fade_to = 0;
static uint32_t s_fade_t0 = 0; static uint16_t s_fade_ms = 0; static bool s_fading = false;
static void star_write(uint8_t level) { s_level = level; ledcWrite(STAR_PWM_CH, level); }

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
    case LED_DEMO: { float ph = (now % 1000) / 1000.0f; uint8_t v = (uint8_t)(25 + 200 * fabsf(sinf(ph * 3.14159f))); c = rgb(0, (uint8_t)(v * 0.8f), v); break; }
    case LED_DEMO_ARM: c = ((now % 1000) < 90) ? rgb(255, 255, 255) : rgb(0, 18, 24); break;
    default: break;
  }
  s_px.setPixelColor(0, c); s_px.show();
}

void star_led_set(bool on) { s_star = on; s_fading = false; if (!s_blinking) star_write(on ? STAR_BRIGHTNESS : 0); }
bool star_led_get() { return s_star; }

// A ramp starts from wherever the LED is now, so a fade-out that interrupts a fade-in does not
// jump. The eye sees roughly the square root of the duty, so the duty follows t^2 on the way up
// and (1-t)^2 on the way down: both ends of the ramp then look smooth instead of snapping.
void star_led_fade(bool on, uint16_t ms) {
  s_star = on; s_blinking = false;
  uint8_t target = on ? STAR_BRIGHTNESS : 0;
  if (ms == 0 || target == s_level) { s_fading = false; star_write(target); return; }
  s_fade_from = s_level; s_fade_to = target; s_fade_t0 = millis(); s_fade_ms = ms; s_fading = true;
}

void star_led_blink_test() { s_fading = false; s_blinking = true; s_blink_step = 0; s_blink_t = millis(); }
// Without this, a blink test still in progress would swallow the demo show's first light step:
// star_led_set() only records the wanted state while s_blinking is true.
void star_led_cancel_blink() { s_blinking = false; star_write(s_star ? STAR_BRIGHTNESS : 0); }

void star_led_tick() {
  if (s_fading) {
    uint32_t el = millis() - s_fade_t0;
    float t = el >= s_fade_ms ? 1.0f : (float)el / (float)s_fade_ms;
    float k = (s_fade_to > s_fade_from) ? t * t : 1.0f - (1.0f - t) * (1.0f - t);
    int level = (int)s_fade_from + (int)lroundf(((int)s_fade_to - (int)s_fade_from) * k);
    star_write((uint8_t)(level < 0 ? 0 : (level > 255 ? 255 : level)));
    if (t >= 1.0f) s_fading = false;
    return;
  }
  if (!s_blinking) return;
  if (millis() - s_blink_t < 250) return;
  s_blink_t = millis();
  if (s_blink_step >= 6) { s_blinking = false; star_write(s_star ? STAR_BRIGHTNESS : 0); return; }
  star_write((s_blink_step % 2 == 0) ? STAR_BRIGHTNESS : 0);
  s_blink_step++;
}
