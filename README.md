# MySat as a real satellite

New firmware for the [MySat 1U CubeSat kit](https://www.mysatkit.com/) (ESP32-CAM + Arduino Nano),
rewritten to behave like a real satellite: FreeRTOS tasks instead of one blocking superloop, a
watchdog and fault isolation, a CRC-protected parameter table, corrected sensor sampling, and a
web dashboard the satellite serves from its own WiFi access point with zero external setup.

Baseline is the stock [MySatKit-Firmware v1.4.1](https://github.com/MySatKit/MySatKit-Firmware).

## Status: phase 0 (foundations) complete, and all three targets now build

| Piece | State |
|---|---|
| `src/aux/` — Nano auxiliary firmware v2 | **compiled and linked** with a real avr-gcc build (`tools/build_aux.sh`), 6.7 KB flash |
| `shared/` — portable core (CRC, parameter table, command tokenizer, orientation triggers, show schedule, ICD) | **57/57 unit tests pass** (`tools/run_native_tests.sh`) |
| `src/obc/` — ESP32-CAM main firmware v2 | **compiled and linked** against Arduino-ESP32 2.0.17 (`tools/build_obc.sh`), 1.06 MB of the 3 MB app slot |
| Deployment sequencer (pull pin → deploy, flip → stow) | written, trigger maths covered by unit tests |
| Demonstration show (pull pin → wings out and back ×2, front light 3 s ×3) | written, schedule and servo-spacing rule covered by unit tests |
| `data/index.html` — web dashboard | written |
| `docs/` — architecture, command reference, build/flash guide | written |

The ESP32 target was written before it could be compiled — the sandbox this repo was bootstrapped
in could not reach the PlatformIO registry or the Arduino board-manager index. Its first real build
found two genuine bugs: a missing `#include "core/log.h"` in `apps/web.cpp`, and a name collision
between this project's `ParamType` enum values (`PT_U8`, `PT_STR`, ...) and the identically-named
ones in Arduino-ESP32's `Preferences.h`, which `core/params_store.cpp` includes alongside the
parameter table. Both are fixed. `tools/build_obc.sh` reproduces that build from GitHub release
assets alone, for environments where the package registries are blocked; with ordinary internet
access `pio run -e obc` is still the supported path.

**Read `docs/FLASHING.md` first** to build and flash. **Read `docs/COMMANDS.md`** for the console/API
command reference — old stock-firmware commands still work, rewritten to the new grammar
automatically. **Read `docs/ARCHITECTURE.md`** for the task diagram, what changed from the stock
firmware and why, and the decisions made from your answers to the phase-0 questions.

## The demonstration show

For showing the cube to people: pull the "remove before flight" pin and the satellite deploys and
folds its solar wings twice, then turns the front light on for three seconds, three times. It runs
off a schedule that a host test checks — two wing cycles, three three-second flashes, and no two
servo commands closer together than the Nano can actually carry out. It is off by default; arm it
once with `demo on` (the setting lives in NVS and survives reflashing), or run it any time with
`demo run`. Everything about it is tunable from `demo.*` parameters. See `docs/COMMANDS.md`.
It works with the Nano firmware the kit ships with (full sweeps, no confirmation) and gets gentler
sweeps and position confirmation once `src/aux/` is flashed. `docs/INSTALL.he.md` walks through
the whole installation step by step.

## The deployment sequence

Pull the launch pin and the satellite powers up, recognises that as separation, counts down, and
deploys its solar panels by itself — then confirms the deployment from the servo's reported angle
and the change in panel current rather than assuming the command worked. Turn it upside down and it
folds the panels back; turn it upright and it deploys again. Which way is "up" is learned from its
own accelerometer the first time it sits still, so it works no matter how the IMU is mounted.
`docs/ARCHITECTURE.md` has the state diagram and the honest limitations; `docs/COMMANDS.md` has the
`mission` commands and the parameters that tune it (including how to switch the 10-second demo
countdown to the real 1800-second CubeSat rule).

## Roadmap

0. **Foundations** (this phase) — PlatformIO, FreeRTOS, no blocking prompts, sampling fixes,
   watchdog, CRC parameter table.
1. **A live satellite** — the launch/deploy sequence (**done**), mission modes + FDIR escalation to
   SAFE, SoC and power modes, quaternion attitude and rotation-rate detection, Morse beacon.
2. **A real link** — binary radio protocol with CRC and auth, store-and-forward, chunked image
   downlink, a ground-station tool, SGP4 orbit propagation.
3. **Operations & science** — anomaly/scenario engine, target-of-opportunity imaging, horizon
   sensor experiment, OTA, an independent EPS watchdog on the Nano.

## Ideas document (Hebrew)

`docs/ideas.he.html` is the earlier planning deliverable this code was scoped from: ~65 feature
ideas across six subsystems (flight software, ADCS, EPS, comms, payload, mission ops), each rated
for coolness/effort/hardware, six 60-second flagship demos, and the roadmap above in full. Open it
in a browser. The six domain-council reports behind it are in `docs/council/*.md` (English).

| מסמך | מה זה |
|---|---|
| `docs/ideas.he.html` | מסמך הרעיונות המלא בעברית: תובנות מהקוד המקורי, כ-65 רעיונות, שש הדגמות דגל, מסלול ושאלות. |
| `docs/INSTALL.he.md` | מדריך התקנה צעד-צעד: VS Code, PlatformIO, צריבת ה-ESP32-CAM וה-Nano, הפעלת ההדגמה. |
| `docs/HARDWARE_BRIEF.md` | תקציר החומרה שנגזר מקריאת הקוד המקורי (אנגלית). |
| `docs/council/*.md` | שישה דוחות מהנדסים לפי תת-מערכת (אנגלית). |
