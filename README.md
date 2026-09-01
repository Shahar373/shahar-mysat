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
| `shared/` — portable core (CRC, parameter table, command tokenizer, ICD) | written, **20/20 unit tests pass** (`tools/run_native_tests.sh`) |
| `src/obc/` — ESP32-CAM main firmware v2 | written, API-checked against every vendored library header by hand; **not yet compiled** — see below |
| `data/index.html` — web dashboard | written |
| `docs/` — architecture, command reference, build/flash guide | written |

**Why the ESP32 side isn't compiled yet:** this repo was built in a network-isolated sandbox where
both the PlatformIO package registry and the Arduino board-manager's tool index were unreachable —
even `arduino-cli` couldn't install the Espressif core without them, and the toolchain + precompiled
libraries run several hundred MB, too large to vendor into git. Everything that *could* be verified
offline was: the Nano firmware built with a real cross-compiler, and 20 unit tests exercise the
shared CRC/parameter/command-parsing logic with plain `g++` (this actually caught and fixed a real
bug — a reference bound into a `packed` struct, which GCC rightly rejects). Full details, including
what to expect on your first `pio run -e obc`, are in `docs/ARCHITECTURE.md`.

**Read `docs/FLASHING.md` first** to build and flash. **Read `docs/COMMANDS.md`** for the console/API
command reference — old stock-firmware commands still work, rewritten to the new grammar
automatically. **Read `docs/ARCHITECTURE.md`** for the task diagram, what changed from the stock
firmware and why, and the decisions made from your answers to the phase-0 questions.

## Roadmap

0. **Foundations** (this phase) — PlatformIO, FreeRTOS, no blocking prompts, sampling fixes,
   watchdog, CRC parameter table.
1. **A live satellite** — mission modes + FDIR escalation to SAFE, launch/deploy sequence, SoC and
   power modes, quaternion attitude and rotation-rate detection, Morse beacon.
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
