# MySat as a real satellite

New firmware for the [MySat 1U CubeSat kit](https://www.mysatkit.com/) (ESP32-CAM + Arduino Nano),
rewritten to behave like a real satellite: FreeRTOS tasks instead of one blocking superloop, a
watchdog and fault isolation, a CRC-protected parameter table, corrected sensor sampling, and a
web dashboard the satellite serves from its own WiFi access point with zero external setup.

Baseline is the stock [MySatKit-Firmware v1.4.1](https://github.com/MySatKit/MySatKit-Firmware).

## Status: phase 0 (foundations) complete

| Piece | State |
|---|---|
| `src/aux/` — Nano auxiliary firmware v2 | written, **compiled and linked** with a real avr-gcc build (`tools/build_aux.sh`), 6.7 KB flash |
| `shared/` — portable core (CRC, parameter table, command tokenizer, orientation triggers, ICD) | written, **34/34 unit tests pass** (`tools/run_native_tests.sh`) |
| `src/obc/` — ESP32-CAM main firmware v2 | written, API-checked against every vendored library header by hand; **not yet compiled** — see below |
| Deployment sequencer (pull pin → deploy, flip → stow) | written, trigger maths covered by unit tests |
| `data/index.html` — web dashboard | written |
| `docs/` — architecture, command reference, build/flash guide | written |

**Why the ESP32 side isn't compiled yet:** this repo was built in a network-isolated sandbox where
both the PlatformIO package registry and the Arduino board-manager's tool index were unreachable —
even `arduino-cli` couldn't install the Espressif core without them, and the toolchain + precompiled
libraries run several hundred MB, too large to vendor into git. Everything that *could* be verified
offline was: the Nano firmware built with a real cross-compiler, and 34 unit tests exercise the
shared CRC / parameter / command-parsing / orientation-trigger logic with plain `g++` (this has
already caught two real bugs — a reference bound into a `packed` struct, which GCC rightly rejects,
and an off-by-one tick in the flip debouncer). Full details, including what to expect on your first
`pio run -e obc`, are in `docs/ARCHITECTURE.md`.

**Read `docs/FLASHING.md` first** to build and flash. **Read `docs/COMMANDS.md`** for the console/API
command reference — old stock-firmware commands still work, rewritten to the new grammar
automatically. **Read `docs/ARCHITECTURE.md`** for the task diagram, what changed from the stock
firmware and why, and the decisions made from your answers to the phase-0 questions.

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
| `docs/HARDWARE_BRIEF.md` | תקציר החומרה שנגזר מקריאת הקוד המקורי (אנגלית). |
| `docs/council/*.md` | שישה דוחות מהנדסים לפי תת-מערכת (אנגלית). |
