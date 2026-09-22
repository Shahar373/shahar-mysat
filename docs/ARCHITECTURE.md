# MySat Firmware v2 — Architecture (Phase 0)

This is the foundations layer for the "MySat as a real satellite" rewrite. It replaces the stock
single-superloop sketch with a small FreeRTOS-based flight software stack, fixes the sampling and
protocol bugs found while reading the stock firmware, and gives every later phase (mission modes,
FDIR escalation, the binary radio protocol, orbit propagation, scenarios...) a stable place to
attach to. See the project root `README.md` for how this fits the overall roadmap, and
`docs/ideas.he.html` for the full menu of future features this was scoped from.

## Decisions made from your answers

| Question | Your answer | What it drove |
|---|---|---|
| Audience | Hobby desk use + short demos for visitors | AP-only WiFi by default: zero setup, works standing on a table with nothing else |
| UI language | English | Console, web UI, events and docs are English (the ideas document stays Hebrew, that was a separate deliverable) |
| Domain you're most excited about | Comms & mission ops | Phase 0 already lays the ICD, event log and web API these two domains build on next |
| IMU model | Unknown | `attitude.imu_whoami` is reported in telemetry (`0x71`=MPU9250 with magnetometer, `0x70`=MPU6500, `0x68`=MPU6050); run `params list` / check the boot log after flashing to find out |
| Second HC-12 / ground PC | None owned yet | Phase 0 keeps the console/radio human-readable text; the binary protocol (comms council report, idea 1) is deferred until you have a second radio to test against |
| Battery | 18650 Li-ion, no USB during demos | `battery_capacity_mah` defaults to 2600; OCV-based SoC (EPS council idea 1) is a phase-1 item, not yet implemented |
| Servo | Holds intermediate angles fine | `AUX_CMD_SERVO_ANGLE` (arbitrary angle) is implemented now, not just open/closed — sets up solar-array-drive experiments later |
| Deployment behaviour | Pull the pin → panels deploy; flip it over → panels fold | Built as the mission sequencer described below, with a learned "up" reference so it works regardless of how the IMU is mounted |
| Soldering | None | No hardware changes anywhere in this phase. The Nano watchdog is software-only (AVR `wdt`); the "external watchdog wired to the ESP32's EN pin" idea from the flight-software council report stays a documented option, not a default |
| Build tooling | Your call | PlatformIO (see "Why PlatformIO" below) |
| OTA vs more photos | Your call | Deferred to phase 1; kept the photo ring modest (8 SVGA) so the decision isn't forced yet |
| Fault injection scope | Your call | Deferred to phase 3 (scenario engine); phase 0 only has the *real* FDIR primitives (watchdog, reset reason, device isolation) that a scenario engine will later exercise |
| Ground track | Fictional orbit, assume no internet at the satellite | Not yet implemented (phase 1/2); noted so SGP4-from-TLE work doesn't assume a live TLE fetch |

## Why PlatformIO

Keeps every stock Arduino library (BSEC, INA3221, ADS1X15, RTC by Makuna, ArduinoJson, NeoPixel)
working via `lib_deps`/vendored `lib/`, adds real unit tests you can run without any board attached
(`pio test -e native`), custom partition tables when OTA is decided later, and one repo with three
build targets (`obc`, `aux`, `native`) instead of two separate Arduino sketch folders. The trade-off:
you'll `pio run` instead of clicking Verify in the Arduino IDE — the extension IDs are listed in
`docs/FLASHING.md` if you'd rather stay inside VS Code with a GUI.

## Two computers, one protocol

```
┌─────────────────────────── ESP32-CAM (OBC) ───────────────────────────┐
│                                                                        │
│  core/          fdir.h        watchdog, reset reason, boot counter,   │
│                                RTC-RAM black box, per-device health   │
│                 params_store  CRC-protected parameter table (NVS,     │
│                                working + golden copy)                 │
│                 mission_clock RTC → system time, MET, clock status    │
│                 events        compact on-board event log (LittleFS)  │
│                 log           leveled logging, also feeds events      │
│                 bus           Telemetry / Housekeeping snapshots      │
│                                shared between tasks (mutex-guarded)   │
│                                                                        │
│  hal/           i2c_bus       the one shared I2C bus + bus recovery   │
│                 nano_link     OBC-side of the AUX protocol            │
│                 leds          SIGNAL LED status language, STAR LED    │
│                                                                        │
│  apps/          sensors       ★ owns I2C. Dedicated 200 Hz IMU loop   │
│                                + 2 Hz env/sun/power/RTC loop           │
│                 mission       separation -> deploy -> flip triggers    │
│                 demo          the bench demonstration show             │
│                 console       one-line commands (serial + web share  │
│                                the same interpreter)                  │
│                 data_logger   mission CSV, hourly rotation            │
│                 camera_app    secondary payload: capture + ring       │
│                 wifi          station-with-AP-fallback, own task      │
│                 web           HTTP API + served UI                    │
│                                                                        │
│  main.cpp       creates every task, runs the 250 ms control tick      │
│                 (housekeeping refresh, AUX heartbeat, event flush,    │
│                 LED state, watchdog feed)                             │
└───────────────────────────────┬────────────────────────────────────--┘
                                 │ I2C (0x08), see shared/mysat_icd.h
┌────────────────────────────────▼──────────────────────────────────────┐
│                      Arduino Nano (AUX)                                │
│  servo (deploy/retract/arbitrary angle), HC-12 power + AT-mode pins,   │
│  AVR watchdog, status readback (Wire.onRequest) — the OBC now gets     │
│  real telemetry back from this board, which the stock firmware never  │
│  did.                                                                  │
└──────────────────────────────────────────────────────────────────────┘
```

`shared/` is compiled into all three targets (`obc`, `aux`, `native`) and must stay free of
Arduino/FreeRTOS types:
- `mysat_icd.h` — the interface control document: AUX command/status wire format, mission mode and
  event code enums.
- `crc.h` — CRC-8/SMBUS (AUX frames), CRC-32 (parameter table), CRC-16/CCITT (reserved for the
  phase-2 radio protocol).
- `params_def.h` — the `Params` struct, its CRC sealing/checking, a name→field table (`params list`
  reads this), and range-clamping (`params_sanitize`).
- `cmdline.h` — the command-line tokenizer and the legacy-command alias table (`SolarDeploy` still
  works, it's rewritten to `solar deploy` before dispatch).
- `attitude_trigger.h` — the orientation maths behind the deployment triggers: a learned "up"
  reference, upright/sideways/inverted classification, a settled-enough-to-judge test, and a
  hold-time debouncer.
- `demo_show.h` — the demonstration show's schedule: a configuration expanded into a list of timed
  steps, plus the spacing rule that keeps the servo commands apart. Portable for the same reason as
  `attitude_trigger.h`: a servo moves because of it, so it is worth testing off-hardware.

## FreeRTOS task map

| Task | Core | Rate | Role |
|---|---|---|---|
| `imu` | 1 | 200 Hz | IMU sampling only — the stock firmware's ~80%-of-rotation-lost bug (burst-read every 500 ms with a clamped first-sample dt) is fixed by giving attitude integration its own steady loop |
| `sensors` | 1 | 2 Hz | env/sun/power/RTC read, publishes the `Telemetry` snapshot, runs a requested IMU calibration |
| `console` | 1 | ~50 Hz poll | non-blocking serial read, periodic telemetry/plotter frame |
| `mission` | 1 | 4 Hz | the deployment sequencer: separation, countdown, deploy with confirmation, orientation triggers. Ticks at 20 Hz while the demonstration show runs, so a "three second" flash is three seconds |
| `control` | 1 | 4 Hz | housekeeping refresh, AUX heartbeat + status poll, event log flush, watchdog feed |
| `leds` | 1 | 50 Hz | SIGNAL LED animation, STAR LED blink-test sequencing |
| `wifi` | 0 | event-driven | station connect with a bounded timeout, falls back to AP; never blocks anyone else |
| `web` | 0 | as needed | `WebServer::handleClient()` |
| Arduino `loop()` | 1 | 1 Hz | idle; just feeds its own watchdog subscription |

Every task calls `esp_task_wdt_add()` once and `esp_task_wdt_reset()` regularly; a task that hangs
reboots the board with reset reason `TASK_WDT`, visible in the next boot's log and in `hk.reset_reason`.

## What actually changed vs. the stock firmware (and why)

- **No blocking prompts.** `ChangeTime`, `SetWIFI`, `SetCallSign`, `SelectPlotterMode` all used
  `while (!Serial.available()) {}`. Over a radio link that's a satellite that hangs waiting for a
  human. Replaced with inline arguments (`time set <ISO8601>`, `wifi ssid <name>`, `callsign <name>`).
- **IMU dt bug.** See the `imu` task note above.
- **INA3221 averaging + bus-voltage correction.** The stock code read one unaveraged sample every
  500 ms and reported the bus voltage as-is, which is sensed *after* the current shunt (≈50 mV low
  at 500 mA). Now averages 64 samples and adds `I × R_shunt` back when `ina.vbus_corr` is enabled
  (default on).
- **Gyro range.** `±250 °/s` saturates if a person spins the cube by hand; now `±1000 °/s`.
- **Camera capture timeout.** `while (!fb)` had no bound; now gives up after 1.5 s and reports the
  failure instead of hanging the whole board.
- **Photos served raw, not base64-in-JSON.** `/api/photo?id=N` streams the JPEG file directly with
  `Content-Type: image/jpeg`; the stock firmware built a base64 string in a heap `String`, which is
  both slower and a fragmentation risk.
- **CRC-protected parameter table** replaces `config.txt`, `callsign.txt`, `/cal.dat` and the
  EEPROM-emulation motor flag with one struct, two NVS copies (working + golden), and range
  validation on every load.
- **AUX now reports status.** The stock Nano firmware was I2C-write-only. `AuxStatus` (servo angle/
  state, radio flags, heartbeat age, boot flags, command/CRC-error counters) is now readable from
  the OBC, and it runs its own AVR watchdog.

## The deployment sequence

This is the behaviour the owner asked for, built the way a real spacecraft does it.

```
   launch pin pulled
   (power applied)          ESP_RST_POWERON  ->  SEPARATION
          |
          v
   [LEOP]  countdown, amber breathing LED, mission clock starts
          |                 default 10 s (real CubeSats: 1800 s, set mission.deploy_inhibit_s)
          v
   [DEPLOYING]  servo commanded open, fast amber blink
          |
          |  confirmation is read back, not assumed:
          |    - AUX reports the angle its servo actually reached
          |    - panel current before/after is logged as secondary evidence
          v
   [NOMINAL]  green heartbeat
          |
          |  turned upside down for flip_hold_s  ->  [STOWED]  (magenta blink)
          |  turned back upright                 ->  [DEPLOYING] -> [NOMINAL]
          v
   any `solar ...` command  ->  [MANUAL], automatic triggers suspended
```

## The demonstration show

The owner also wanted a routine for showing the satellite to people: pull the pin, and instead of
one deployment the cube performs a fixed act. `demo on` arms it; after that a power-on reset runs
the show rather than the launch sequence above.

```
   launch pin pulled        ESP_RST_POWERON  +  demo.enabled  ->  [DEMO]
          |
          |  5 s arming hold (put the cube down, step back), cyan LED
          v
   wings out -> wings in, twice           each sweep 2.6 s + 0.4 s of rest, amber LED
          |                               every sweep's end angle is read back from the AUX
          v
   front light on 3 s, off 1 s, 3x        STAR LED on GPIO14
          |
          v
   [NOMINAL] or [STOWED] depending on where the wings ended  (~29 s in total)
```

Three things make this more than a list of delays:

- **The schedule is data, not control flow** (`shared/demo_show.h`). It expands into a list of
  timed steps, which lets a host test assert the properties that matter -- two wing cycles, three
  three-second flashes, the light never left on at the end, and above all that no two servo
  commands are closer together than the AUX controller can carry out.
- **The servo spacing is a checked invariant, not a comment.** The AUX powers the servo for
  `AUX_SERVO_POWER_MS` and ignores a new movement while a sweep is running, so a command sent too
  early only retargets the sweep in progress. That constant now lives in the ICD, both firmwares
  use it, `demo_cfg_sanitize()` enforces it on every load and `params set`, and
  `demo_min_servo_gap_ms()` is what the test asserts on.
- **The mission sequencer holds the phase for the whole show.** `MPHASE_DEMO` is what keeps the
  orientation triggers off the mechanism while it runs -- there is exactly one owner of the servo
  at any moment. A `solar ...` command stops the show and takes it back.

Honest limitations, the same ones the deployment sequence has: the servo has no position feedback,
so "confirmed" means the AUX reported finishing at the angle it was asked for. Four sweeps per show
is four times the mechanism wear of a single deployment -- that is the point of the show, but it is
worth knowing before leaving it running in a loop. And `demo stop` cannot abort a sweep already in
progress, because the protocol has no halt and stopping half way is worse for the mechanism than
finishing.

**A power-on reset is the separation event; a software or watchdog reset is not.** If the
satellite reboots in flight it does not re-run its deployment. That distinction comes from
`esp_reset_reason()`, and it is the same reason a real spacecraft latches its deployment state.

**"Which way is up" is learned, not hard-coded.** The IMU's orientation relative to the cube faces
is not documented for this kit and differs between board revisions, so the satellite records its
own gravity vector the first time it is set down and left still for three seconds, and every later
orientation decision is the angle between the current gravity vector and that reference
(`shared/attitude_trigger.h`). `mission learn-upright` re-records it. Consequences worth knowing:
- Tipping the satellite onto a *side* face reads as `sideways` and triggers nothing — only a real
  flip past 120 degrees counts.
- A reading is only judged when the satellite is settled: about 1 g total and rotating slower than
  25 deg/s. While it is being carried, the last decision stands.
- An orientation must hold for `mission.flip_hold_s` (default 2 s) before it acts, and two servo
  movements are never closer together than `mission.actuation_gap_s` (default 5 s).

The trigger maths lives in a portable header precisely so it can be unit tested: a servo moves
because of these decisions, so `test/test_core` covers upright/inverted/sideways classification
with an arbitrary learned axis, the settled test rejecting shaking and tumbling, and the
debouncer's timing and reset behaviour.

**Open-loop limitation, stated honestly:** the servo has no position feedback. The AUX controller
reports the angle it *commanded*, which is why the panel-current comparison is logged alongside it.
If the panels are moved by hand while the satellite is off, the AUX and reality disagree until the
next full deploy or retract sweep.

## What is intentionally *not* here yet

Mission mode manager, autonomous SAFE mode entry, the binary radio protocol, SGP4, TRIAD, sun-vector
attitude, the scenario engine, and OTA are all phase 1–3 per `docs/ideas.he.html`'s roadmap. The
`MissionMode` enum and `LED_SAFE`/`LED_FAULT` states already exist so phase 1 has somewhere to land
without another restructure.

## A note on how this was built

All three targets now build from source, and all three builds are reproducible offline-ish (GitHub
release assets only, no PlatformIO or Arduino package registry):

| Target | How it is verified | Result |
|---|---|---|
| `aux` (Nano) | `tools/build_aux.sh` — real avr-gcc against ArduinoCore-avr 1.8.6 + Servo | links, 6736 B flash / 499 B RAM |
| `native` (host) | `tools/run_native_tests.sh` — plain g++ | 52 tests pass |
| `obc` (ESP32-CAM) | `tools/build_obc.sh` — arduino-cli against Arduino-ESP32 2.0.17 and the xtensa-esp32-elf 8.4.0 toolchain | links, 1 057 585 B (33% of the huge_app slot), 55 160 B static RAM |

The ESP32 target was written before it could be compiled — the sandbox this repository was
bootstrapped in could not reach the PlatformIO registry or the Arduino board-manager index — and
its first real build found exactly the class of problem that predicts: a missing `#include` for
`core/log.h` in `apps/web.cpp`, and a name collision between this project's `ParamType` enum
(`PT_U8`, `PT_STR`, ...) and the identically-named `PreferenceType` values in Arduino-ESP32's
`Preferences.h`, which `core/params_store.cpp` includes alongside `shared/params_def.h`. Both are
fixed; the enum is now `PARAM_U8` / `PARAM_STR` / ... for that reason.

`tools/build_obc.sh` is a build check, not a replacement for PlatformIO: it exists because the
package registries are blocked in some environments, exactly as `tools/build_aux.sh` does for the
Nano. With ordinary internet access `pio run -e obc` is still the supported path, and it is what
`docs/FLASHING.md` describes.
