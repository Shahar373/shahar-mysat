// SIGNAL LED (NeoPixel on GPIO2) status language + STAR LED (white, PWM on GPIO14).
#pragma once
#include <Arduino.h>

enum LedState : uint8_t {
  LED_BOOT = 0,        // white breathing while subsystems come up
  LED_NOMINAL,         // green heartbeat
  LED_NO_LINK,         // blue slow blink: WiFi station not connected
  LED_AP_MODE,         // amber solid: serving our own access point
  LED_FAULT,           // red double blink: a sensor is isolated or params were restored
  LED_SAFE,            // red slow breathing (phase 1)
  LED_LEOP,            // amber breathing: separation seen, counting down to deployment
  LED_DEPLOY,          // fast amber blink: the servo is moving the panels
  LED_STOWED,          // magenta slow blink: panels folded after being turned over
  LED_ACTIVITY,        // short cyan flash on command / photo, then back to previous
  LED_DEMO,            // cyan sweep: running the bench demonstration show
};

void leds_init();
void leds_set(LedState s);
void leds_flash_activity();
void leds_tick();               // call at >= 20 Hz from the control tick
void star_led_set(bool on);
bool star_led_get();
void star_led_blink_test();     // 3 blinks then restore, non-blocking
void star_led_cancel_blink();   // drop a blink test in progress and hand the LED back to star_led_set
void star_led_tick();
