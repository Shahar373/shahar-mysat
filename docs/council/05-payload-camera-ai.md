# Payload & Onboard Data Processing — Ideas Council Input

Source facts used: JPEG-only XGA q15 capture with `fb_count=1` and a discard-a-frame hack, a blocking `while(!fb)` loop, base64 into `String` + `DynamicJsonDocument(fb->len*1.4)`, BSEC in continuous (1 Hz) mode inside the single `loop()`, PWDN on GPIO32, on-board flash LED on GPIO4, complementary-filter Euler angles from the MPU, and four ADS1015 photoresistors (front = ph4). No FreeRTOS tasks, no free GPIOs.

## Baseline reality check (numbers that constrain everything)

| Item | Size / time | Where |
|---|---|---|
| XGA JPEG q15 frame | ~60–120 KB | PSRAM (driver default when PSRAM present) |
| XGA decoded RGB888 | 2.36 MB | PSRAM only; avoid |
| XGA JPEG decoded at 1/8 scale (`esp_jpg_decode`, `JPG_SCALE_8X`) | 128×96 gray = 12 KB, ~80–150 ms | internal RAM — **this is the workhorse** |
| QQVGA JPEG capture + decode | 160×120, ~4 KB, ~40 ms total | cheap 1 Hz "watch mode" |
| LittleFS (Huge APP partition) | ~1 MB usable ⇒ 10 XGA photos ≈ the whole disk | flash |
| Free internal heap after WiFi + camera + BSEC | ~100–150 KB, fragmented by `String` | the real bottleneck |

Two fixes before any feature: replace the `while(!fb)` spin with a timeout, and set `fb_count=2`, `grab_mode=CAMERA_GRAB_LATEST`, `fb_location=CAMERA_FB_IN_PSRAM` instead of discarding a frame. Stream photos to the web client in chunks (`server.sendContent`) instead of a 500 KB base64 `String`.

## Ideas

### 1. EO Product Metadata (L0 → L1 sidecar)
Every capture writes `photo_N.jpg` plus `photo_N.json`: product ID `MYSAT_L1A_20260901T120000_00042`, RTC UTC, attitude quaternion (Euler→quat from `mpu_data`), gyro rate (motion-smear flag), 4 sun-sensor readings + sun-in-FOV flag, computed solar elevation/azimuth from RTC + configured lat/lon (NOAA algorithm, ~40 lines), battery V/mA, board temp, exposure/gain read back via `s->get_reg(s, 0x45/0x10/0x04, ...)` and `s->status.aec_value/agc_gain`. Real EO products (Sentinel, Planet) are useless without this ancillary data; it makes geolocation and radiometry possible. **Cool 4 / Effort S / HW none.** Gotcha: MPU must be sampled *immediately* before `esp_camera_fb_get`; in JPEG mode the frame is ~50–100 ms old already.

### 2. Sun-Sensor Pre-Exposure + Sun-in-FOV Protection
Before capture, read ph4 (front face — confirm camera boresight is on that face). If very bright: `set_ae_level(-2)`, `set_aec2(1)`; if dark: `+2`, `set_agc_gain` up. If saturated: refuse or flag ("sun avoidance"). Real cameras set integration time from predicted sun elevation, and instruments have sun-avoidance constraints. **Cool 3 / Effort S / none.** Gotcha: OV2640 AEC needs 2–4 frames to settle after a change; with `GRAB_LATEST` grab twice.

### 3. Thumbnail + Tiered Downlink Products
From each XGA JPEG, decode at 1/8 (12 KB gray or 37 KB RGB888), re-encode with `fmt2jpg()` q30 → ~2–3 KB thumbnail. Keep 30 thumbnails + 8 full frames; ROI crop endpoint (`/product?id=42&roi=x,y,w,h`) decodes at 1/2 or 1/4 scale and crops. Real missions send browse imagery first, full data on request. **Cool 4 / Effort M / none.** This also fixes the storage crisis (item table above) and makes the 9600-baud HC-12 realistic: 3 KB ≈ 4 s instead of 100 KB ≈ 2 min.

### 4. Image Statistics → Compact Telemetry
On the 1/8 gray image: mean, std, 16-bin histogram, bright fraction (>200 ⇒ "cloud/snow-like"), dark fraction, edge density (3×3 gradient count), saturation flag. ~5 ms compute. Emit as a 40-byte L2 record in telemetry instead of the image. Real EO pipelines compute cloud masks and quality flags onboard or in L1 processing. **Cool 3 / Effort S / none.**

### 5. Change-Detection "Target of Opportunity"
Watch mode: QQVGA JPEG every 1–2 s, decode to gray, `|frame − prev|` per pixel, % changed > threshold ⇒ `EVENT`, then switch to XGA (`set_framesize`, no re-init needed in JPEG mode), take the full product, log `TOO_TRIGGER`. Also triggerable by a light step on the photoresistors or a ground command. Real satellites re-task on volcano/fire alerts (e.g. Sentinel-2 to Sentinel-1 triggers). **Cool 5 / Effort M / none.** Gotcha: AEC hides slow light changes; compare after normalising by mean; ignore first 3 frames after resize.

### 6. Horizon Sensor Experiment
Point at a table edge / window with bright "sky" above dark "ground". On the 128×96 image, per column find the row of max vertical gradient; least-squares fit a line ⇒ roll angle and pitch offset. Compare to MPU roll/pitch and report the residual. Real LEO satellites use Earth-horizon sensors for coarse attitude. **Cool 5 / Effort M / none.** Honest: works only with a clean two-tone scene; that is exactly what a classroom demo can arrange.

### 7. Night Mode / Star Tracker Lite
Turn off AEC/AGC (`set_exposure_ctrl(0)`, `set_gain_ctrl(0)`), `set_aec_value(1200)`, `set_agc_gain(30)`, and lower XCLK with `s->set_xclk(s, LEDC_TIMER_0, 5)` (5 MHz) so a frame takes ~0.3–0.5 s ⇒ that is the true max exposure of an OV2640; longer needs frame stacking (average 8 QVGA gray frames = 8× SNR). "Stars" = cardboard with pinholes and a phone light behind, in a dark room. Detect blobs > threshold, output centroid list; match triangle side-ratios against a stored pinhole catalogue ⇒ identified "constellation" and rotation angle. Real star trackers do exactly centroid + pattern matching. **Cool 5 / Effort L / optional: pinhole card.** Gotcha: OV2640 dark noise is high; do dark-frame subtraction (idea 8) first.

### 8. Camera Health & Calibration (BIT, dark, flat)
Commands: `CamTest` runs `set_colorbar(1)` and verifies a known pattern (data-path built-in test), `DarkFrame` (lens covered) computes mean/σ per 1/4-scaled pixel and stores a hot-pixel map (`/cal_dark.bin`, ~20 KB), `FlatField` (white paper) stores a vignetting profile. L1 stats are dark-subtracted and flat-corrected. Real instruments take dark and flat calibrations routinely; degradation trending is a health metric. **Cool 3 / Effort M / none.**

### 9. Tiny ML Scene Classifier — with honest limits
ESP32 (not S3) has no vector unit. TFLite Micro person-detection (96×96 int8 MobileNet-v1-0.25, ~300 KB flash, ~140 KB arena) runs ~0.6–2.5 s per inference depending on esp-nn; the arena does not fit internal heap alongside WiFi, so `ps_malloc` it in PSRAM (≈2× slower). ESP-DL face detection ran on plain ESP32 in Arduino core 2.0.x (`HumanFaceDetectMSR01`), 200–400 ms at QVGA RGB565 — that requires `PIXFORMAT_RGB565` which means `esp_camera_deinit()` + re-init to switch (~0.5 s), 150 KB frame. Core 3.x dropped it for non-S3. Verdict: **feasible as "one classification every 10 s, output a 1-byte class + confidence"**, not as video. Real payloads (ESA Φ-sat-1) run onboard cloud classification to avoid downlinking useless frames. A hand-crafted alternative (histogram + edge features + a 3-class decision tree) costs 1 ms and teaches the same lesson. **Cool 4 / Effort L / none.** Gotcha: BSEC, WiFi and a TFLM interpreter are each "heavy" libraries; expect linker/heap fights in Arduino IDE.

### 10. BME680 Atmospheric Science Suite (L2 products)
Hypsometric altitude `h = 44330·(1−(P/P0)^0.1903)` with a ground-reference command (`SetQNH`) — a stair climb shows ~0.12 hPa/m; BME680 noise ≈0.1 hPa, so average 10 s to resolve 1 m. Dew point (Magnus), pressure tendency (3-h slope ⇒ Zambretti "forecast" word), and gas resistance re-badged as **outgassing/contamination monitor**: a solvent marker near the vent drops resistance 50% in seconds and raises an `CONTAMINATION` event. Real satellites monitor pressure/humidity during ground ops and contamination sensors near optics. **Cool 4 / Effort S / none.** Gotcha: the BME680 sits next to a 60–70 °C ESP32; temperature is board temperature, not air. Report both "internal" and a corrected estimate; switch BSEC to `LP` (3 s) mode to cut heater power.

### 11. Product Catalogue + Priority Storage + Compression
`catalog.json`: product ID, level (L0/L1/L2), size, priority, downlinked flag. Deletion policy: oldest L0 full frames first, L2 records and thumbnails protected. Telemetry log: delta-encode int16 channels + heatshrink (window 2^8, ~1.5 KB state) ⇒ CSV shrinks 3–5×; buffer 4 KB in RAM before each LittleFS write (small appends are the real wear killer, not photos). Real missions have a mass-memory file system with priorities and a downlink queue. **Cool 3 / Effort M / none.**

### 12. Payload Duty Cycling & Thermal Guard
`esp_camera_deinit()` + `digitalWrite(32, HIGH)` between campaigns; re-init ~400 ms, discard 3 frames for AEC. Measure with INA3221 battery current before/after ⇒ live "payload power budget" telemetry (expect 40–80 mA delta). Refuse captures above a configurable board temperature ("payload safe mode"). **Cool 3 / Effort S / none.**

## Flagship 60-second demo: "Target of Opportunity"

Satellite sits in watch mode, telemetry shows `IMG_STATS` every 2 s and a flat change metric. A visitor waves a hand ⇒ change spikes ⇒ NeoPixel flashes amber, console prints `EVENT TOO #7 Δ=38%`, the camera jumps to XGA, captures, and within 2 s the web GUI shows the thumbnail, the full product metadata (product ID, quaternion, sun-sensor state, exposure) and the L2 stats line; the full image is queued as "pending downlink". Then the visitor tilts the satellite and the horizon-fit roll (idea 6) tracks the MPU roll on screen. Every piece maps to a real mission concept in one sentence each.

## Questions for the user

1. Which face is the camera on relative to the four photoresistors (is ph4/"front" the camera boresight)? Is the lens the 66° stock or a wide/fisheye variant?
2. Which partition scheme is flashed (how much LittleFS actually exists), and is OTA desired (it halves it)?
3. Must WiFi stay on during imaging, or may imaging campaigns run WiFi-off (frees ~50 KB heap and lets ML/stacking breathe)?
4. Is the demo environment indoor classroom (favors horizon/TOO/pinhole-star demos) or outdoor window (favors cloud fraction, sun elevation)?
5. Appetite for ESP-IDF-style components (TFLM/ESP-DL) versus staying pure Arduino-library? This decides whether idea 9 is realistic.
