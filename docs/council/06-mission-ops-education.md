# Ideas Council — Operations & Experience Domain (MySat 1U kit)

**Baseline observations that shape everything below** (from `server.h`, `console.h`, `MySat_main.ino`):
- The GUI is one ~1100-line HTML/JS string embedded in `server.h`, polling `/get_data` every 2 s over XHR; the JSON is ~30 flat fields. Nothing is stateful on the sat side beyond `motor_state`, `logging_state`, `callSign`.
- Several console commands block the whole loop (`while (!Serial.available()) {}` in `setCallSign`, `selectPlotterMode`, `setWiFi`). Because UART0 *is* the radio, an interactive prompt sent over HC-12 hangs the "spacecraft" until a ground reply arrives. Any ops concept must remove blocking prompts first.
- Event log is a 100-line ring; mission CSV capped at 3600 rows. Storage for a real "mission history" is tight (~1–2 MB LittleFS).
- The NeoPixel currently only says "WiFi yes/no" — a huge unused expressive channel.

## Feature ideas

### 1. Mission Lifecycle State Machine ("Phases")
**What:** Sat lives in `PRELAUNCH → LAUNCH → LEOP → COMMISSIONING → NOMINAL → SAFE → EXTENDED → EOL`. Each phase gates commands (e.g. `SolarDeploy` refused in LEOP until a 30-min deploy-inhibit timer expires or a `DeployOverride` with confirm), changes telemetry cadence, and drives the NeoPixel pattern (LEOP = slow amber breathing, NOMINAL = green heartbeat, SAFE = red double-blink, EOL = fade to off). Phase, phase-entry time and MET are persisted in NVS so a reboot doesn't reset the mission.
**Why real:** Every mission has a Flight Rules document keyed to phases; deploy inhibits exist because separation-time actuation can kill you (CubeSat P-POD rules require 30+ min inhibit).
**Implement:** `mission.h` enum + `Preferences` keys (`phase`, `phase_t0`, `met_t0`); a `canExecute(cmd)` table consulted by `handleCommands()` and every HTTP handler; add `phase`, `met_s`, `orbit_n` to `/get_data`. Pre-launch is the only phase where WiFi setup prompts are allowed.
**Cool 4 · Effort M · HW none.** Gotcha: the user must be able to `ForcePhase` for demos; log it as an OVERRIDE event so it's honest.

### 2. Real MCC Dashboard (PC ground station, not on the ESP32)
**What:** Move the GUI off the sat. Keep the ESP32 serving *only* `/get_data`, `/cmd`, `/events`, photo endpoints, plus a tiny fallback page. The rich MCC (subsystem pages EPS/ADCS/TCS/PAYLOAD/COMMS, limit-check colours, alarm annunciator with acknowledge, command history with ACK/NACK, MET/UTC/pass clocks, 3D cube from roll/pitch/yaw, image gallery) becomes a single-file HTML app on the laptop that talks to the sat's IP (or, later, to a serial-bridge over the HC-12).
**Why real:** Nobody runs the MCC on the spacecraft; the ground segment is where limits, displays and history live. It also removes a memory/flash tax from the OBC and lets the UI grow without reflashing firmware.
**Implement:** Static HTML+JS (Bootstrap already in `data/`), `fetch` every 1 s; limits table JSON (yellow/red per field); `localStorage`/IndexedDB for history so the browser holds hours of telemetry the sat cannot. Add CORS header on ESP32. Optional Python/Node bridge for serial.
**Cool 4 · Effort L · HW none.** Gotcha: hard fork of the current UX; keep the on-board fallback page so the kit still "works alone".

### 3. Limit Checking + Alarm Annunciator + Honest "REAL/SIM" tags
**What:** Every telemetry point gets yellow/red high/low limits (battery <3.5 V yellow, <3.3 V red; temp; gyro rate; panel current asymmetry). Out-of-limit triggers an annunciator tile that stays lit until acknowledged, a sat-side EVENT (`LIMIT_RED battery_v 3.28`) and a NeoPixel red flash. Each tile shows a small badge: **REAL** (measured), **DERIVED** (SoC estimate, orbit count) or **SIM** (injected anomaly, orbit model).
**Why real:** Limit monitoring + annunciator is the core FC job; and honesty about simulation is what keeps the educational value legit.
**Implement:** Limits enforced on the sat (so they work over radio) in a `limits.h` table stored as `/limits.json`; UI mirrors them. `/get_data` gains an `alarms` bitmask.
**Cool 4 · Effort S/M · HW none.**

### 4. Anomaly / Scenario Engine with Certification
**What:** `StartScenario <id>` injects a fault into the telemetry pipeline or actuators: `BATT_CELL` (battery_v reads −0.4 V and sags under load), `STUCK_SUN` (ph3 frozen), `TUMBLE` (gyro rates + ~30°/s pseudo-random walk), `MEM_CORRUPT` (checksum mismatch on `/cal.dat`, attitude flagged INVALID), `PANEL_SHADOW` (right panel current ×0.1), `RADIO_DEGRADED` (random 30 % of frames dropped/garbled). Operator must diagnose from telemetry and issue the correct recovery (`Recalibrate`, `SafeMode`, `SwitchPanel`, `ReloadCal`, `Reboot`). Timer + score + a debrief page: time-to-detect, time-to-recover, wrong commands sent. Levels: FC-L1 (single obvious fault) → L3 (two concurrent faults, silent one).
**Why real:** Simulation ("sim sup") training is exactly how NASA/ESA certify controllers; anomalies in sims are injected *between sensor and display*, never in the hardware.
**Implement:** A fault layer between `read_sensors` and both telemetry outputs; scenario definitions in `/scenarios.json`; scores in NVS; debrief rendered from the event log (which needs to grow to ≥500 lines or a second ring file). Never touch real battery or servo for faults.
**Cool 5 · Effort L · HW none.** Gotcha: a student must always be able to tell the fault is simulated (SIM badge, tone on LED); scenario must auto-end after N minutes so nobody flies a "broken" sat forever.

### 5. Built-in Operating Procedures (checklists)
**What:** Procedures as JSON: "OP-001 Power-on Checkout", "OP-004 Solar Deploy", "OP-010 Enter Safe Mode", "OP-020 Recover from Tumble". Each step = instruction, optional command button, expected telemetry condition that auto-ticks green. Completion is logged (`PROC OP-004 COMPLETE by EPS`).
**Why real:** Nothing is commanded ad hoc; controllers execute procedures with verification steps.
**Implement:** Ground-side UI component; conditions expressed as `field op value`; procedure text bilingual (EN/HE) in the same JSON.
**Cool 3 · Effort M · HW none.**

### 6. Multi-Role Classroom Mode
**What:** Roles FD, EPS, ADCS, PAYLOAD, COMMS. Each browser picks a role; view filters to that subsystem's page + annunciator; only FD (or the role owner) can send that subsystem's commands; commands require a "GO" from FD in a shared comm-loop panel. Sat side just records `by:<role>` in the command event.
**Why real:** Flight control rooms are role-partitioned with a Flight Director gate; it also gives 5 kids something to do simultaneously with one sat.
**Implement:** Role token in `/cmd?role=EPS`; ESP32 `WebServer` handles ~4–6 concurrent pollers if the period is 2 s and photos are not fetched simultaneously (measure; the current handler is synchronous). Shared state (GO/NO-GO) either polled from the sat (`/room`) or via a laptop hub.
**Cool 4 · Effort M · HW none.** Gotcha: ESP32 WebServer is single-threaded; a photo download blocks others for ~1 s.

### 7. Campaign & Science Missions
**What:** Long-running goals with a progress meter and auto-report: "24 h environmental survey" (temp/hum/pressure/IAQ hourly), "Photo every hour for a day", "Sun-tracking day" (which face saw the most light, per hour), "Battery cycle study". At completion the sat writes `/report_N.txt` (human-readable summary + stats) and the UI renders charts.
**Why real:** Science ops are campaign-driven, and mission success criteria are measured, not felt.
**Implement:** Reuse `data_logger.h` cadence; add a `campaign.h` scheduler using RTC time; storage: hourly averages only (24 rows), photos limited to the existing ring of 10 → bump to 24 at lower JPEG quality (XGA q15 ≈ 60–80 KB, 24 × 40 KB SVGA fits).
**Cool 3 · Effort M · HW none.** Gotcha: IAQ needs 5-min warm-up and BSEC state; a reboot mid-campaign must resume from NVS.

### 8. Mission Journal (auto-narrated)
**What:** Converts the event log into a human sentence stream: "MET 02:14:07 — Right solar panel current dropped below 5 mA for 3 min; battery discharging at 120 mA. Entered SAFE. Recovered by command from COMMS at 02:19:40." Downloadable as Markdown; bilingual.
**Why real:** Console logs / shift handover notes are a real deliverable.
**Implement:** Templated strings ground-side from a richer event schema (`type,code,val,role`); sat stores the compact form.
**Cool 3 · Effort S · HW none.**

### 9. Satellite "Personality": Boot Sequence, Beacon, Light Language
**What:** Boot: NeoPixel runs a POST sequence — one colour per subsystem as it initialises, red hold on the failed one (matches `init_status`). Every 60 s a **beacon**: callsign in Morse on the STAR LED (PWM) and a one-line compact frame on Serial/HC-12 (`MYSAT B 0042 3.87V 24C NOM`). A consistent "status light language" card in the UI so kids learn to read the sat from across the room.
**Why real:** Real CubeSats beacon Morse/CW callsigns (many amateur sats do), and status LEDs on the bench mirror mode.
**Implement:** Non-blocking Morse timer on GPIO14; beacon uses existing `callSign` and frame counter. Also fixes the blocking-prompt problem: replace prompts with one-line commands (`SetCallSign 4X1ABC`).
**Cool 4 · Effort S · HW none (optional: piezo buzzer on a free Nano pin for audible CW).**

### 10. Time & Orbit Model
**What:** Show UTC and Israel local, MET, mission day, orbit number and "day/night" from a simulated 93-min LEO with 60/40 sun/eclipse split; the light-sensor total from the ADS1015 overrides the sim when the real room is bright ("REAL sun detected"). Optional ground track on a static world map image (SGP4 is overkill; a simple inclined circular orbit is fine and labelled SIM).
**Why real:** Every MCC display has MET/UTC/orbit counters and an eclipse indicator driving power planning.
**Implement:** DS3231 for UTC; `orbit_n = floor(MET / 5580 s)`; NTP once WiFi is up to correct the RTC (currently set by hand).
**Cool 3 · Effort S · HW none.**

### 11. Compare-with-Space Panel
**What:** A ground-side panel that loads a CSV/JSON of a real CubeSat's public telemetry (SatNOGS DB export, downloaded once by the user) and plots battery voltage / temperature of a real sat next to MySat's — "your battery is at 3.9 V, X-CubeSat's was 4.1 V at this MET".
**Why real:** Connects the kit to actual space data and teaches why values look the way they do.
**Implement:** Purely ground-side file import; no live internet required from the ESP32. Clear labelling that the comparison sat is unrelated.
**Cool 3 · Effort S · HW none.** Gotcha: SatNOGS decoders vary per sat; pick one with a clean dashboard export.

### 12. Bilingual (HE/EN) + Young-Student Accessibility
**What:** All UI strings from a dictionary; `dir="rtl"` toggle; large-tile "Kid mode" with four traffic-light gauges (Power, Heat, Spin, Comms) and icons instead of numbers. Keep engineering units on hover.
**Implement:** i18n JSON ground-side; sat-side event codes stay language-neutral (numbers/codes) so translation is a display concern. Use CSS logical properties so the whole MCC mirrors.
**Cool 2 · Effort S/M · HW none.** Gotcha: Morse callsign and callsigns stay Latin; Hebrew fonts need proper fallback stack.

## Flagship 60-second demo: "Pass 17 — Panel Fault"
Visitor sees the MCC on a laptop: MET clock ticking, orbit 17, green annunciator, cube rotating live as you tilt the sat. Press **Start Scenario: PANEL_SHADOW**. Within 5 s the right-panel current tile goes yellow, then red; the annunciator klaxon lights, the sat's NeoPixel switches from green heartbeat to red double-blink, and the beacon frame changes to `…SAFE`. The visitor is handed the tablet as "EPS": procedure OP-020 opens with three steps; they click *Compare panel currents* (auto-ticks), *Command SolarRetract/Deploy re-cycle* (servo audibly moves), then *Clear fault*. Currents equalise, annunciator clears on acknowledge, NeoPixel returns to green, and a debrief card appears: "Detected in 6 s, recovered in 41 s — Flight Controller Level 1 provisional." Everything real (attitude, currents, servo) is labelled REAL; the fault is labelled SIM.

## Questions the user must answer first
1. **Audience:** solo hobbyist at a desk, a classroom of 5–30 students, or a competition/exhibit? (Drives ideas 6, 12 and how much goes ground-side.)
2. **UI language:** Hebrew-first with English engineering terms, fully bilingual, or English only? Who reads it — you, or 12-year-olds?
3. **Ground station:** are you willing to run an HTML file / small Python bridge on a laptop, or must everything stay on the ESP32 with zero PC software?
4. **Radio:** do you own a second HC-12 (do we design ops around a real RF link with dropouts), or is WiFi the only "link"?
5. **Realism vs safety:** is it acceptable for scenarios to *truly* actuate (e.g. cycle the servo, dim the STAR LED to load the battery), or must all anomalies stay purely in telemetry?
