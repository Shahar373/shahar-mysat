# OBC / C&DH Council Brief — MySat "Real Satellite" Firmware

Key observations from the source that shape the recommendations below (verified in code, not assumed):

- `setup()` blocks forever if the DS3231 has invalid time (`readUARTTime()` spins on `while(!Serial.available())`), and `tryConnectWiFi()` loops until a human types "Yes/No". `pauseToRead()` is a 3 s blocking delay sprinkled everywhere; `calibrateMPU()` blocks ~7 s; `reactToCommand()` sleeps 2 s. A satellite must never wait for a human.
- Superloop, single core, no watchdog, no `esp_reset_reason()`, no boot counter. Heartbeat is written to NVS every 5 s (`prefs.begin/putUInt/end`), motor state goes through the EEPROM-emulation NVS partition.
- Nano is a write-only I2C slave (`Wire.onReceive`, no `onRequest`), no AVR watchdog enabled.
- Camera runs `fb_count=1`, XGA; partition scheme is almost certainly "Huge APP (3 MB, no OTA)".

## Feature ideas

### 1. Mission Mode Manager
**What:** Explicit state machine: `LEOP → COMMISSIONING → NOMINAL ↔ SCIENCE`, plus `LOW_POWER`, `SAFE`, `EMERGENCY`. Each mode is a table row: which apps run, telemetry rate, WiFi on/off, camera allowed, LED colour. Transitions fire from events (battery V, reset reason, ground TC, timers) and are logged.
**Why real:** Every spacecraft (cFS, F Prime, PUS) constrains behaviour by mode so FDIR has something well-defined to fall back to.
**How:** `enum Mode` + `const ModeConfig MODE_TABLE[]` + `transition(Mode, Reason)` guarded by a legality matrix. Persist current mode and entry reason in NVS; on boot, `SAFE` if last mode was SAFE or reset reason ≠ `ESP_RST_POWERON`. NeoPixel (GPIO2) = mode colour; STAR LED PWM = mode-dependent "payload heater".
**Cool 4 · Effort M · HW none.** Gotcha: every existing blocking prompt must be replaced or modes are meaningless.

### 2. FDIR Core: Watchdog, Reset Forensics, Autonomous Safe Mode
**What:** Task watchdog on all tasks; on boot read `esp_reset_reason()`, increment a boot counter, and enter SAFE if the reason is WDT/panic/brownout or if >3 resets in 10 min. SAFE = WiFi off (`WiFi.mode(WIFI_OFF)`, ~80 mA saved), camera off (`esp_camera_deinit()`), `setCpuFrequencyMhz(80)`, beacon only, accept TCs.
**Why real:** The "reboot-loop → safe mode" ladder is the single most important survival mechanism on any CubeSat.
**How:** `esp_task_wdt_init()` / `esp_task_wdt_add()` / `esp_task_wdt_reset()` (config struct in arduino-esp32 core 3.x). Keep boot counter, last-mode, and a small "black box" ring in `RTC_NOINIT_ATTR` memory (8 KB RTC slow RAM survives soft resets, zero flash wear) and mirror to NVS only on mode change. Stop the 5 s NVS heartbeat: use `RTC_NOINIT_ATTR uint32_t last_alive_s`. Sensor health: per-device consecutive-I2C-error counters → mark device FAILED, drop from HK, retry with backoff (`Wire.endTransmission()` return codes already available).
**Cool 5 · Effort M · HW none.** Gotcha: the ESP32 cannot power-cycle its own peripherals; a stuck I2C device needs a bus-recovery routine (9 clock pulses on SCL via `Wire.end()` + bit-bang GPIO13).

### 3. Nano as Independent EPS/Watchdog Controller
**What:** Turn the Nano into a real auxiliary computer: enable its own AVR watchdog, answer `Wire.onRequest` with a status struct (servo state, HC-12 power, uptime, its own boot count, a heartbeat counter). ESP32 polls it; Nano expects an ESP32 "I'm alive" byte every ≤10 s.
**Why real:** Separate EPS/watchdog MCUs (e.g. GomSpace NanoPower, ISIS iOBC supervisor) survive OBC crashes.
**How:** `wdt_enable(WDTO_8S)` from `<avr/wdt.h>`; extend the 1-byte command set to `[cmd][arg][crc8]`; `Wire.onRequest` → 8-byte status. Without extra wiring the Nano can only *react* (e.g. cut HC-12 power via D5, blink SOS) when the ESP32 heartbeat stops.
**Cool 3 (5 with the wire) · Effort S–M · HW optional:** one wire from Nano D2 to the ESP32-CAM RST-button pad (open-collector via NPN or configure D2 as input-normally, output-low-to-reset) gives a true external hardware watchdog. Requires soldering to the ESP32-CAM RST pad; verify the board revision exposes it.

### 4. PUS-style Packet Link (TC/TM with CRC & ACKs)
**What:** Replace free text with CCSDS-flavoured packets: 6-byte primary header (APID, sequence count, length) + PUS-1 acceptance/completion ACKs + CRC-16/CCITT, COBS-framed so bytes are unambiguous over the transparent HC-12 pipe. A short text "shell" stays available for the USB console (frames start with `0x00` delimiter; anything printable is shell).
**Why real:** Every ESA/university CubeSat speaks ECSS-E-ST-70-41 PUS; ACKs are how you know a command arrived through a lossy link.
**How:** Small `packet.h` (pure C, testable on host); python `gs/` tool with pyserial decoding TM to a live table. HC-12 needs `AT+B115200` (or firmware drops to 9600) — currently unclear the radio actually matches the 115200 console.
**Cool 4 · Effort M · HW none** (second HC-12 for a real RF ground station: optional, ~$5). Gotcha: half-duplex, no collision avoidance → TM cadence must leave TC gaps.

### 5. Onboard Scheduler (Time-Tagged & Relative Sequences)
**What:** PUS-11 style: upload TCs with an absolute UTC execution time or relative offsets ("in 300 s take photo, +5 s downlink thumbnail"). Persisted in LittleFS so it survives reboot; cleared on SAFE entry.
**Why real:** Passes are 8 minutes; everything else is pre-scheduled.
**How:** Sorted vector of `{exec_time, packet}`; scheduler task wakes each second, compares against `time(NULL)` (set from DS3231, idea 11). Commands: insert / delete / list / enable-disable / time-shift.
**Cool 4 · Effort M · HW none.** Gotcha: RTC must be valid; refuse absolute-time TCs when clock status is "not synchronised".

### 6. Housekeeping Telemetry & Limit Monitoring
**What:** Periodic HK packet: `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`, `ESP.getFreePsram()`, `temperatureRead()` (internal die temp, ±10 °C on classic ESP32 — flag it), `LittleFS.usedBytes()`, uptime, boot count, reset reason, mode, per-task `uxTaskGetStackHighWaterMark()`, I2C error counters, WiFi RSSI, Nano status. PUS-12 monitors each parameter against low/high limits with a persistence count, raising events that FDIR consumes.
**Why real:** HK is 90 % of what a real sat downlinks; limit monitoring is the sensor of FDIR.
**Cool 3 · Effort S · HW none.** Gotcha: `vTaskGetRunTimeStats` needs `configGENERATE_RUN_TIME_STATS`, not enabled in arduino-esp32 — skip CPU %.

### 7. CRC-Protected Parameter Table
**What:** One `struct Params` (mode thresholds, telemetry rates, callsign, WiFi creds, camera quality, inhibit durations…) with version + CRC-32, two copies in NVS (working + golden). PUS-20 get/set/commit/restore. Replaces `config.txt`, `callsign.txt`, EEPROM motor state.
**Why real:** Tuning without re-flashing; guaranteed-sane defaults after corruption.
**How:** `esp_crc32_le()` from `esp_crc.h`; load → verify → fall back to golden → fall back to compiled defaults, logging each step.
**Cool 3 · Effort S · HW none.**

### 8. "Radiation Storm" Mode: Fault Injection, Scrubbing, TMR
**What:** A TC (or GS button) flips random bits in the parameter table, the mode variable, or a scheduler entry; a 1 Hz scrubber task detects CRC mismatches and repairs from golden; critical scalars (mode, inhibit flags) are stored as triplicated `TMR<T>` with majority vote on read. Escalation option: inject a deliberate infinite loop → watchdog → SAFE (idea 2).
**Why real:** SEU mitigation is standard; teams test it on the bench because you cannot test it on orbit.
**Cool 5 · Effort S–M · HW none.** Gotcha: honest labelling — the ESP32 has no ECC and no real radiation; this is a *demonstration* of the software mechanism.

### 9. FreeRTOS Task Architecture with Message Bus
**What:** Task per subsystem: `comms` (UART RX/TX, packet parser), `sensors` (I2C owner), `payload` (camera), `hk`, `fdir/modes`, `scheduler`, `web`. Publish/subscribe via FreeRTOS queues carrying a `Msg{topic, payload}`. Only `sensors` touches `Wire`; others request data via the bus.
**Why real:** cFS Software Bus / F Prime ports; isolation means one hung app cannot stall telemetry.
**How:** `xTaskCreatePinnedToCore()`; WiFi/web on core 0 (where the WiFi stack lives), I2C+control on core 1. Wire is not thread-safe → mutex or single-owner. BSEC must be called continuously (its `run()` scheduling depends on it) — it lives in the sensors task at 3 s cadence, never blocked by camera capture.
**Cool 3 · Effort L · HW none.** Gotcha: camera capture with `fb_count=1` blocks ~200 ms and needs 100+ KB PSRAM; `esp_camera_fb_get` from a task other than the one that called `esp_camera_init` is fine, but never from two tasks.

### 10. OTA Firmware Upload with Rollback
**What:** Upload new firmware over WiFi (`Update.h` via `/update` POST or `ArduinoOTA`), boot it, and only mark it valid after it has run 60 s and passed self-test; otherwise the bootloader falls back.
**Why real:** Software uploads are routine; a bad image must not brick the satellite.
**How:** Custom `partitions.csv`: nvs 24 K, otadata 8 K, app0 1.5 M, app1 1.5 M, littlefs ~900 K. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on in arduino-esp32; call `esp_ota_mark_app_valid_cancel_rollback()` after health check.
**Cool 4 · Effort M · HW none.** Gotcha: two app slots halve LittleFS. Current app (camera + BSEC + embedded Bootstrap HTML) is likely ~1.3–1.6 MB, so the 10-photo XGA ring (~60–100 KB each) will no longer fit — drop to 5 photos or SVGA, and move the web UI to the LittleFS image. Simulating "upload over radio" is infeasible: 1.5 MB at 9600 baud is ~25 min with zero error correction.

### 11. Mission Clock: RTC ↔ System Time, MET, Drift Log
**What:** At boot copy the DS3231 into ESP32 system time (`settimeofday`), so `time()`, LittleFS timestamps and scheduler all agree. Maintain Mission Elapsed Time from a "launch epoch" in NVS. When WiFi is up, run SNTP and *measure* DS3231 drift instead of blindly overwriting; write the DS3231 only on ground TC. Clock status (`UNSYNC/RTC/NTP`) in HK.
**Why real:** Time correlation is a formal ground-segment function; everything downstream depends on it.
**Cool 3 · Effort S · HW none.** Gotcha: removes the blocking `readUARTTime()` prompt — invalid RTC becomes an HK flag plus a `SET_TIME` TC, not a boot hang.

### 12. Launch Sequence & Deployment Inhibits
**What:** LEOP simulation: "separation switch" event (TC, or a jumper) starts a persisted timer; deployables inhibited 30 min and RF transmit inhibited 45 min (CDS rules; scale to 3/4.5 min for demos via parameter). Then automatic panel deploy through the Nano, deploy confirmation via INA3221 panel current rising, then COMMISSIONING checks each sensor and transitions to NOMINAL.
**Why real:** Every CubeSat carries this; forgetting it is a launch-provider violation.
**How:** Persist separation time in NVS so a reboot at T+10 min does not restart the count. Nano D2 with a pull-up + jumper wire = separation switch (optional, ~free); servo 2.2 s power cut stays as-is.
**Cool 4 · Effort S · HW optional.** Gotcha: motor state currently in EEPROM emulation and commanded blind in `setup()` before `Serial.begin`; replace with a mode-driven deploy.

## Tooling & repository

**Move to PlatformIO + Arduino framework** (not pure ESP-IDF). Reasons: keep all current libraries (Adafruit, Makuna RTC, WebServer, BSEC's precompiled `libalgobsec.a` works via `lib_deps` + `build_flags`), gain `board_build.partitions`, `-D GIT_SHA`, two environments in one repo (`esp32cam`, `nanoatmega328`), host-native Unity tests for pure-logic modules (CRC, scheduler, mode table, packet parser), and GitHub Actions CI. Pure ESP-IDF would mean re-porting every sensor driver — not worth it for one person.

```
mysat-fsw/
  platformio.ini            # envs: obc (esp32cam), aux (nanoatmega328), native (tests)
  partitions/ota_littlefs.csv
  shared/mysat_icd.h        # command IDs, APIDs, struct layouts shared by OBC/Nano/GS
  obc/src/core/   bus, modes, fdir, scheduler, params, packet, time
  obc/src/hal/    i2c_bus (mutex owner), nano_link, radio_uart, storage
  obc/src/apps/   eps, adcs, payload_cam, payload_env, hk, comms, web
  obc/data/       LittleFS image: web UI, default params
  aux/src/        Nano firmware
  gs/             Python ground station (pyserial, decoder, curses/web dashboard)
  test/           Unity tests (native)
  docs/           ICD (packet & TC catalogue), mode diagram, FDIR table
```

## Flagship 60-second demo: "Solar flare"

Visitor presses **FLARE** on the ground-station laptop. 0–10 s: radiation mode injects bit flips; the scrubber reports `PARAM_CRC_FAIL → RESTORED_FROM_GOLDEN`, then `MODE_TMR_VOTE_CORRECTED`, each as an event on screen. 10–15 s: a third injection hangs the payload task; the watchdog fires, NeoPixel goes dark. 15–30 s: reboot; the beacon reads `RESET=TASK_WDT BOOTS=7 MODE=SAFE`, NeoPixel slow red, WiFi off, free-heap and battery in every 10 s beacon. 30–50 s: operator sends `MODE NOMINAL`; the sat replies ACCEPTED then COMPLETED, LED turns blue, the pre-uploaded schedule resumes and takes a photo at the time-tag. Firmware-only, and every mechanism above is on stage.

## Questions for the user

1. Do you own a second HC-12 (real RF ground station), or is USB the only link? And has the HC-12 been set to 115200 (`AT+B115200`), or is the radio currently receiving garbage at 9600?
2. Are you willing to solder one wire from the Nano to the ESP32-CAM reset pad (true external watchdog), and add a jumper as a "separation switch"?
3. Accept fewer/smaller photos (5 × SVGA) in exchange for OTA with rollback?
4. Will you move to VS Code + PlatformIO, or must it stay Arduino IDE 2.x (which rules out custom partitions, shared headers across boards, and host tests)?
5. Does the power board have battery protection (BMS/undervoltage cutoff) and what is the minimum safe cell voltage? Is USB usually plugged in during demos (if yes, battery-driven FDIR needs a "simulated undervoltage" TC)?
