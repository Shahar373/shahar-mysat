# MySat — Communications & Ground Segment Ideas

**Ground truth from the source:** the HC-12 rides on UART0 with USB; `SetRadio` does nothing (Nano `RF_SET` is an empty `break`); ESP32 `Serial` is 115200 but HC-12 default is 9600; several commands spin on `while(!Serial.available())`, which over a radio hangs the sat; every `LOG_*`/`logDebug`/`reactToCommand` banner is transmitted. Everything below assumes we fix those first.

## Feature ideas

### 1. MYSAT-TM/TC binary framing (CCSDS-lite)
Replaces free-text with fixed frames; keeps a `text` mode reachable by typing `TEXT\n` three times.
- **Why real sats:** CCSDS Space Packets give unambiguous boundaries, error detection and packet accounting. Noise on a transparent 433 MHz pipe otherwise becomes "command unrecognized".
- **How:** `SYNC 0x1A 0xCF | TYPE(1: TM/TC/ACK/NACK/BEACON/FILE) | APID(1) | SEQ(2) | LEN(1) | PAYLOAD(≤64) | CRC-16/CCITT-FALSE(2)` = 9 B overhead. Sync word is the real CCSDS ASM prefix. State-machine parser with resync; drop on CRC fail. TC gets ACK(seq) within 200 ms or ground retries ×3. Keep payload ≤64 B and a 10–20 ms inter-frame gap — HC-12 buffers overflow on long bursts.
- **Coolness 4 · Effort M · HW none.** Gotcha: USB console becomes unreadable in binary mode — the Python GS (idea 8) must do the decoding; Arduino Serial Monitor is retired.

### 2. Command authentication + anti-replay
Every TC carries a 32-bit counter and a 4-byte truncated HMAC-SHA256 over the frame with a shared 16-byte key stored in NVS. Counter must exceed the last accepted one (sat persists it). Unauthenticated frames log `AUTH_FAIL` and are counted in telemetry.
- **Why:** real operators are terrified of hijack/replay; CCSDS SDLS does exactly this.
- **How:** ESP32 `mbedtls_md_hmac()`; ground `hmac.new(key, frame, sha256).digest()[:4]`. Key upload only via USB "umbilical" text mode, never over the air. Keep `PING` unauthenticated so anyone can hear the sat but nobody can command it.
- **Coolness 4 · Effort S · HW none.** Gotcha: counter desync after reflash → add a `SYNC_COUNTER` handshake.

### 3. Amateur-style beacon (text + Morse)
Every 30 s (10 s during a pass) the sat sends `BEACON` frame: callsign, uptime, mode, Vbat, Ibat, T, free FS, cmd count, boot count, last error (~24 B). Simultaneously the STAR LED keys the callsign and a health letter in Morse (~12 WPM), and optionally the ASCII Morse string "-.-. --.-" goes out in text mode.
- **Why:** IARU-coordinated CubeSats identify with callsign; CW telemetry beacons (e.g. "letter = battery bucket") let hams decode health by ear.
- **How:** non-blocking dot/dash scheduler on `ledcWrite(14)`; beacon payload as packed struct. Ground decodes and shows "last heard".
- **Coolness 4 · Effort S · HW none.** Gotcha: user's `callSign` must be a fake/unlicensed indicator ("4X-MYSAT-1" style, no real prefix) unless licensed.

### 4. Half-duplex discipline, log levels, quiet mode
One UART carries debug prints, telemetry, and inbound commands; collisions corrupt both directions.
- **How:** (a) route all `LOG_*` into an EVENT frame type with level; `SET_LOGLEVEL` TC; default `WARN` over air, `DEBUG` only in umbilical mode. (b) Sat is master: after each TM/beacon it stays silent for a 150 ms "uplink window"; GS transmits only inside it (it sees end-of-frame). (c) Remove every `while(!Serial.available())`; parameters ride in the TC payload. (d) Detect USB attached? Not possible on ESP32-CAM — instead a `UMBILICAL` TC toggles verbose mode.
- **Why:** every real TT&C link is scheduled; TM and TC never talk over each other.
- **Coolness 2 · Effort M (touches all modules) · HW none.** Gotcha: this is the unglamorous foundation everything else needs.

### 5. Store-and-forward with simulated passes
Sat is "out of contact" most of the time: telemetry is written to LittleFS ring (~4 B/s at 1 frame/10 s ≈ 1.2 MB/month). A pass opens by (a) RTC schedule — 8 min every 95 min mimics LEO — or (b) ground `HELLO` with auth. During the pass the sat dumps everything since `last_acked_seq`, newest first, then live TM; at LOS it goes back to beacons.
- **Why:** LEO sats see a ground station ~4×10 min/day; store-and-forward is the core of every mission.
- **How:** binary records with the same SEQ; `DUMP_FROM(seq)` TC; ground keeps a "gap list". At 9600 baud (~700 B/s payload) one day of 10 s records (~8600 × 30 B = 260 kB) takes ~6 min — realistic tension for the pass timer.
- **Coolness 5 · Effort M · HW none.** Gotcha: photos vs telemetry compete for ~1.5 MB flash — need quotas.

### 6. Chunked image downlink with selective repeat (CFDP-lite)
`FILE_INFO(id, size, n_chunks, crc32)` then `FILE_CHUNK(id, idx, 64 B)`. Ground writes chunks into a bitmap and shows a progressively filling image (grey blocks for missing). After the burst, ground sends `NACK_BITMAP` (up to 480 chunk flags in 60 B); sat resends; repeat until complete.
- **Honest rates:** current XGA q15 JPEGs are ~40–80 kB. Effective HC-12 throughput ≈ 700 B/s at 9600 (air rate 15 kbps, framing, gaps), ≈ 6–8 kB/s at 115200 (air 236 kbps, but range collapses to tens of metres). A 30 kB image: **~45–60 s at 9600, ~5 s at 115200**; a 60 kB XGA at 9600 is ~2 min — longer than a real pass. So: first send a QVGA thumbnail (~6–10 kB → 10–15 s), full res on request or over WiFi (idea 9).
- **Why:** CCSDS CFDP class 2 does exactly this; progressive reveal is how real SSTV/image downlinks feel.
- **Coolness 5 · Effort M · HW none.** Gotcha: `sensor->set_framesize()` on the fly to get thumbnails; JPEG isn't tolerant to gaps — display only from the last complete chunk or use grey fill.

### 7. Link quality without RSSI
HC-12 exposes no RSSI. Measure what we can: ground computes packet error rate from SEQ gaps and CRC failures per minute; sat counts CRC-fail vs good TC and echoes them in the beacon; a `LINK_TEST(n)` sends n numbered frames for a PER curve. GS shows "S-meter" bars derived from PER and a running Eb/N0-like estimate. Optional: dual-baud test to show range vs rate trade-off.
- **Why:** operators watch link margin every pass; PER is the honest metric here.
- **Coolness 3 · Effort S · HW none.** Gotcha: don't fake an RSSI number — label it PER.

### 8. Python ground station (PC or Raspberry Pi)
`pyserial` + PyQt6/pyqtgraph (or a Flask+WebSocket page) with: frame decoder, live plots, yellow/red limit alarms (Vbat < 3.5 yellow, < 3.3 red; T > 45 red; PER > 20%), command console with HMAC signing and ACK tracking, pass timer/countdown, image reassembly panel, SQLite archive + CSV export, event/beacon log, raw hex view. Optional sub-features: MQTT publish (`paho-mqtt`) so a second laptop/phone dashboard (Node-RED/Grafana) follows the pass SatNOGS-style; "network mode" where a friend's station relays frames it heard.
- **Why:** the GS software is half of any mission; SatNOGS proves many stations > one.
- **HW: needed — second HC-12 + USB-TTL adapter (~$5–8 total) or a spare Nano as USB bridge.** Effort L (the biggest item) · Coolness 5. Gotcha: the GS HC-12 must match channel/baud; ship a `gs_config.py` that does the AT setup.

### 9. Two-band architecture: HC-12 = UHF TT&C, WiFi = "S-band" payload link
WiFi is normally OFF (saves ~100 mA, more "realistic" power budget). A `PAYLOAD_LINK_ON(duration)` TC (or the pass scheduler) brings WiFi up; full-res photos and CSV logs move over HTTP fast; the web GUI works only during passes. If no known SSID: AP mode `MYSAT-xxxx` with captive portal (`DNSServer`) to enter credentials. When WiFi is up, `configTime()` NTP sets the DS3231 and reports clock drift in TM (real sats do time correlation). OTA via `ArduinoOTA`/HTTP update **only** over WiFi.
- **OTA over HC-12: infeasible in practice** — ~1.2 MB firmware at 700 B/s ≈ 30 min best case, hours with retries, and a bad flash bricks it. Real sats patch in small blocks; the analog here is a `PARAM_TABLE_UPLOAD` (limits, beacon period, key rotation, pass schedule ≤ 1 kB) — do that instead.
- **Coolness 4 · Effort M · HW none.** Gotcha: 4 MB flash + camera + OTA needs the "Minimal SPIFFS/OTA" partition scheme; check LittleFS size fits.

### 10. HC-12 management from firmware: channel, power, frequency agility
Implement `RF_SET` on the Nano (pull D4 low), then the ESP32 speaks AT at 9600 (`AT+Cxxx`, `AT+Pn`, `AT+Bxxxx`, `AT+RX` to read config), release SET, restore baud. Expose `RADIO_SET(ch, pwr, baud)` TC with a **two-phase commit**: sat ACKs, both sides switch at an agreed SEQ, sat reverts to home channel if no frame in 60 s. "Low-power mode" lowers TX to `AT+P3` (~8 dBm) — visible in PER.
- **Legal honesty:** HC-12 CH1 = 433.4 MHz, 400 kHz spacing. In Israel/EU only 433.05–434.79 MHz is licence-free ⇒ **channels 1–4 only**; CH5+ sits in 435–438 MHz, the actual amateur *satellite* band — nice teaching moment, not a place to transmit without a licence.
- **Coolness 3 · Effort M (Nano firmware + ESP32) · HW none.** Gotcha: AT responses and USB console share the wire; ESP32 must parse `OK+...` while suppressing TM. Also fix the 9600/115200 mismatch by *choosing* one and documenting it.

### 11. BLE "umbilical" for pre-launch checkout (optional)
NimBLE Nordic-UART service: phone app (Serial Bluetooth Terminal/nRF Connect) gets verbose text mode, key provisioning, RTC set, self-test, without touching the radio. Disabled by `LAUNCH` command ("umbilical disconnect").
- **Why:** real sats have an umbilical for T-0 checkout; RF is for flight.
- **Coolness 3 · Effort M · HW none.** Gotcha: BLE + WiFi + camera + PSRAM squeeze RAM/flash; may be mutually exclusive with WiFi at runtime — test early, and it eats the OTA partition budget.

### 12. Nano telemetry readback (enabler)
Add `Wire.onRequest` on the Nano to report servo angle, HC-12 power state, and free-pin ADC (e.g. a thermistor on A0) so the beacon includes "wing angle" and radio status.
- **Coolness 2 · Effort S · HW none.** Gotcha: I2C slave on ATmega with Servo library — keep the ISR tiny.

## Flagship 60-second demo: "A pass over the ground station"
0 s — sat on the table, silent except the STAR LED blinking its callsign in Morse; laptop shows "Next AOS 00:00:08", last beacon health letter.
8 s — AOS: GS sends signed `HELLO`; sat ACKs, LED goes to pass pattern, stored telemetry floods in and the battery/temperature plots fill *backwards* in time.
20 s — visitor clicks "Take photo"; ACK in 200 ms; a thumbnail builds block by block with a progress bar; three grey blocks remain → "Requesting 3 missing chunks" → they fill in.
45 s — visitor types a command with a wrong key: sat replies `NACK AUTH_FAIL`, event log turns red, beacon's `bad_cmd` counter increments.
55 s — LOS countdown hits 0; sat switches to beacon-only, PER gauge greys out, "Next AOS 01:34:52". Everything the visitor saw happened over 433 MHz with a $3 module.

## Questions for the user
1. Do you own a **second HC-12** (and a USB-TTL adapter or spare Nano) for the ground station? Without it the whole domain is USB-only.
2. Is the HC-12 physically on the **ESP32's TX/RX (UART0)**, or on the Nano's UART? Photo of the board / board version (v1.5.5 vs 1.5.6+) would settle it.
3. What baud is the HC-12 currently set to — was it ever configured with `AT+B115200`? Does the radio link actually work today?
4. Ground station platform: Windows/Linux PC or Raspberry Pi? Python acceptable, or web-only?
5. Is a `.py` desktop app OK, or do you want the dashboard on a phone (drives MQTT/web choice)? Do you have (or want) an amateur licence, or should we stay strictly in ISM channels 1–4?
