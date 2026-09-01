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

## FreeRTOS task map

| Task | Core | Rate | Role |
|---|---|---|---|
| `imu` | 1 | 200 Hz | IMU sampling only — the stock firmware's ~80%-of-rotation-lost bug (burst-read every 500 ms with a clamped first-sample dt) is fixed by giving attitude integration its own steady loop |
| `sensors` | 1 | 2 Hz | env/sun/power/RTC read, publishes the `Telemetry` snapshot, runs a requested IMU calibration |
| `console` | 1 | ~50 Hz poll | non-blocking serial read, periodic telemetry/plotter frame |
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

## What is intentionally *not* here yet

Mission mode manager, autonomous SAFE mode entry, the binary radio protocol, SGP4, TRIAD, sun-vector
attitude, the scenario engine, and OTA are all phase 1–3 per `docs/ideas.he.html`'s roadmap. The
`MissionMode` enum and `LED_SAFE`/`LED_FAULT` states already exist so phase 1 has somewhere to land
without another restructure.

## A note on how this was built

This repository was bootstrapped in a network-isolated sandbox. The AUX (Nano) firmware and the
`native` unit test suite were **actually compiled and run** here (see `tools/build_aux.sh` and
`tools/run_native_tests.sh` — both work offline against a locally-cloned ArduinoCore-avr, no
PlatformIO registry needed). The ESP32 (`obc`) target could **not** be compiled here: both the
PlatformIO package registry and the Arduino board-manager's own tool index (needed even to install
the Espressif core via `arduino-cli`) were unreachable from this sandbox, and the ESP32 toolchain
plus precompiled Arduino-ESP32 libraries are several hundred MB, not something to vendor into git.
Every third-party API call in `src/obc/` was cross-checked by hand against the actual vendored
library headers in `lib/`, but you should still expect to fix a handful of real compiler errors on
your first `pio run -e obc` — see `docs/FLASHING.md`.
