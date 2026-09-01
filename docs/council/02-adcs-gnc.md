# ADCS / GNC Council Report — MySat 1U

## Findings from the source that shape everything below

- **Attitude sampling bug (fix first).** `get_mpu_data()` reads 20 samples at 200 Hz (100 ms, blocking) and is called every 500 ms. The first sample's `dt` (~400 ms) is clamped to 5 ms, so **~80 % of all rotation is never integrated**. Any real ADCS work needs a dedicated FreeRTOS task pinned to core 1 at 100–200 Hz with an I2C mutex (BSEC/BME680 also uses `Wire`), or the MPU FIFO (`USER_CTRL 0x6A`, `FIFO_EN 0x23`).
- MPU config today: `0x1A=3` (DLPF 41 Hz), `0x19=4` (200 Hz), ±250 °/s (131 LSB/°/s), ±2 g (16384 LSB/g). Yaw is gyro-only and unbounded drift.
- Sun sensors: ADS1015 with default 2/3× gain (±6.144 V, 3 mV/LSB) → a 3.3 V rail reads ~0–1100, not 0–2047. The GUI uses `brightness = (1023-raw)/1023`, i.e. **darker = higher ADC** (LDR on the high side) and clips at 1023. Face map: ph4=+X front, ph2=−X back, ph3=+Y right, ph1=−Y left. No ±Z faces.
- Body frame must be defined: which IMU axis is "front"? Unknown until measured.

## Feature ideas

### 1. ADCS task + rate telemetry + detumble state machine
Runs the IMU at 200 Hz in its own task; publishes body rates ω (°/s), |ω|, and a state: `TUMBLING` (>15 °/s), `DETUMBLING`, `STABLE` (<0.5 °/s for 10 s). Logs peak rate since "separation".
*Real sats:* first thing after P-POD ejection is a B-dot detumble; ground watches rates decay.
*Implement:* `xTaskCreatePinnedToCore`, gyro bias from `/cal.dat`, low-pass |ω|. Hysteresis on thresholds. Cool 3 · **S** · none · Gotcha: I2C mutex; ±250 °/s saturates when you spin it by hand — switch `0x1B` to ±500 or ±1000.

### 2. Quaternion attitude with Madgwick/Mahony (replaces complementary filter)
6-DoF (or 9-DoF with idea 4) filter producing `q = [w,x,y,z]`, plus derived Euler for humans. Yaw stays bounded only with the magnetometer.
*Real sats:* attitude is always a quaternion (no gimbal lock, composable).
*Implement:* Madgwick ~30 flops/update, trivial on ESP32; β≈0.05. Expose `q` in `/get_data` and in the text frame. Cool 3 · **S** · none · Gotcha: axis-sign conventions (NED vs body); sensor is not aligned with cube faces — see Q1.

### 3. Coarse sun sensor → sun vector
Cosine model `I_i = I₀·max(0, n̂_i·ŝ)`. With opposed pairs: `s_x = (I_front − I_back)/I₀`, `s_y = (I_right − I_left)/I₀`, `s_z = ±√(1 − s_x² − s_y²)` — **sign of z is unobservable** (no top/bottom faces). Report azimuth plus an "elevation magnitude" and a validity flag; declare "sun not in FOV / eclipse" when all four are below a dark threshold.
*Real sats:* exactly how coarse photodiode sun sensors work; the z-ambiguity is a real design flaw fixed by 6-face coverage.
*Implement:* linearize LDRs first (they're logarithmic in lux) with a per-face lookup table calibrated against a fixed lamp; `I₀` = max over faces. Cool 4 · **M** · optional: 2 LDRs on Nano A0/A1 for ±Z (Nano needs `Wire.onRequest` to report them). Gotcha: room reflections break the cosine model; use a dark room.

### 4. Magnetometer (AK8963) as the second reference vector
Read the 3-axis field in µT, hard/soft-iron calibrated, like a real CubeSat magnetometer.
*Implement:* `WHO_AM_I 0x75` → `0x71`=MPU9250 (has AK8963), `0x70`=MPU6500 (does not). Enable bypass: `INT_PIN_CFG 0x37 = 0x02`; AK8963 at `0x0C`, `WIA 0x00 = 0x48`; read ASA fuse ROM (`CNTL1=0x0F`, regs `0x10–0x12`), then `CNTL1 0x0A = 0x16` (16-bit, 100 Hz); burst `0x03–0x09` (must read ST2), little-endian, 0.15 µT/LSB; axes are swapped vs. accel (mx=ay, my=ax, mz=−az). Calibration: figure-eight, min/max or ellipsoid fit, saved to NVS. Cool 4 · **M** · none if MPU9250; otherwise optional QMC5883L/LIS3MDL on I2C (~$2). Gotcha: servo, battery leads and the HC-12 all distort the field; Israel: ~44 µT, inclination ≈ 48°, declination ≈ 5° E.

### 5. TRIAD attitude determination (deterministic, two vectors)
Body vectors: gravity (accel) + magnetic field, or gravity + sun. Reference vectors: down and local field (or a user-entered lamp direction). `t₁=v₁, t₂=(v₁×v₂)/|·|, t₃=t₁×t₂`, `A = [t₁ t₂ t₃]_body·[t₁ t₂ t₃]_refᵀ`, then DCM→quaternion. Compare against the Madgwick estimate and report the disagreement angle as an "ADCS health" metric.
*Real sats:* TRIAD/QUEST is the textbook on-board method; gravity here plays the role of the nadir vector from an Earth sensor (on the desk, gravity **is** nadir — a nice teaching point). Cool 4 · **S** (given 4) · none · Gotcha: degenerate when vectors are near-parallel (lamp overhead); require angle > 20°.

### 6. Separation event + launch-and-early-orbit sequence
Tap/shock (|a| − 1 g > 1.5 g and jerk) or a physical "remove-before-flight" signal marks **separation**. Then a timeline like the CubeSat Design Spec: RF silence and no deployables for 30 min (demo: 30 s), detumble phase, then "antenna/solar deploy" via the servo, first beacon.
*Implement:* software detector at 200 Hz or MPU wake-on-motion (`WOM_THR 0x1F`, `MOT_DETECT_CTRL 0x69`); mission elapsed time in NVS. Cool 5 · **S** · none · Gotcha: false triggers when the cube is set down — require a stable 1 g reading before arming.

### 7. Virtual actuators: reaction-wheel / B-dot simulator
No physical torque exists, so run an honest **software model**: 3-wheel inertia `I·ω̇ = −τ_w`, wheel momentum with saturation (e.g. 1 mN·m·s), PD on quaternion error (`τ = −Kp·q_e,vec − Kd·ω`). Mode A: "sun-point" — the simulated body is initialized from the measured attitude and slews to the measured sun vector, wheel RPMs shown ramping up. Mode B: B-dot detumble using measured `dB/dt` from the magnetometer, showing the torque a magnetorquer would produce. Both display side-by-side "measured (real cube)" vs "simulated (if we had wheels)".
*Real sats:* exactly the controllers flown; momentum management is a classic ops topic. Cool 4 · **M** · none · Gotcha: make the UI say "SIM" loudly; don't let anyone think the cube turns itself.

### 8. Solar array drive (SADA) with the servo
Extend the Nano protocol to 2 bytes (`0x10, angle`) and rotate the wings to the best 1-DoF sun angle from front/back LDR ratio, or by **perturb-and-observe** on INA3221 CH2+CH3 current (maximize watts, like an MPPT).
*Real sats:* SADAs track the sun using sun sensors or ephemeris. Cool 4 · **M** · none · **Gotcha:** the 2.2 s power cut-off exists because the servo stalls against the mechanism; intermediate angles may not be mechanically meaningful — verify (Q4). Rate-limit to one move/minute; keep the Nano's attach/detach logic.

### 9. On-board SGP4 orbit propagation from a TLE — **feasible**
Store a TLE in LittleFS, uploaded over the console/HC-12 (`SetTLE`, a genuine "uplink"). Propagate every second using DS3231 time → Julian date → ECI → ECEF (GMST) → lat/lon/alt; sub-satellite point on a map. Sun ephemeris (Meeus low-precision) gives a cylindrical-shadow **eclipse flag** used to drive the "simulated power state" and the STAR LED. Pass predictor for the user's lat/lon: step 10 s over 24 h ≈ 8 640 propagations (double math is software on ESP32, ~0.2–0.5 ms each → a few seconds once at boot on core 0); output AOS/LOS/max elevation; **beacon over HC-12 only during passes**, store-and-forward otherwise.
*Libraries:* Hopperpop's `Sgp4` Arduino library (ESP-tested, includes pass prediction) or Vallado's `sgp4unit.cpp`. Memory: <10 KB. Use the ISS TLE for real daily passes over Israel, or a fictitious 500 km SSO "MYSAT-1".
Cool 5 · **M** · none · Gotcha: TLE ages (re-upload weekly); RTC must be within seconds of UTC — add an NTP sync path.

### 10. Camera as Earth-horizon sensor / mini star tracker
Horizon: in a dark room a lit paper globe = Earth; grab a grayscale QVGA frame, threshold, take limb edge points, least-squares circle fit → nadir offset from the optical axis (OV2640 lens ≈ 66° FOV). Star tracker: 6–10 ceiling LEDs; centroid blobs, match triangles (angle triples) against a stored catalog → lost-in-space attitude.
*Real sats:* the two highest-accuracy optical sensors on any spacecraft. Cool 5 · **L** · optional: LEDs, a lamp. Gotcha: `esp_camera_fb_get` blocks ~100–300 ms and holds PSRAM; do it in the ADCS-idle slot; auto-exposure must be locked.

### 11. 3D attitude cube + ground track in the web GUI
Quaternion → CSS `matrix3d` cube (no library, six faces labelled with PH1..PH4 and panel positions) plus sun and magnetic vectors as arrows; an equirectangular SVG world outline with the SGP4 sub-satellite dot, ground trace, terminator and the user's station footprint. three.js is possible but ~600 KB; CSS3D is zero-dependency and fits LittleFS.
Cool 4 · **S/M** · none.

### 12. Desk verification kit
Lazy Susan or record player (33⅓ rpm = 200 °/s — a perfect gyro scale check); rotate exactly 360° and require integrated yaw = 360 ± 2°. Phone flashlight at 1.5 m in a dark room as the Sun (parallax over a 10 cm cube < 4°); protractor on the table vs. estimated sun azimuth. Phone compass vs. magnetometer heading. CSV logger already exists → Python replay of the same filter offline (add a `ReplayLog` unit-test path so the algorithm is testable off-hardware). Cool 2 · **S** · optional: lazy Susan.

## Flagship 60-second demo: "Deploy, detumble, find the Sun, wait for a pass"
Lights off, one lamp. Tap the cube on the table — GUI flashes **SEPARATION**, mission clock starts, RF silence countdown, rates 40 °/s shown as you spin it on the lazy Susan, state `TUMBLING → STABLE` as it settles; the 3D cube on screen mirrors it live. At T+30 s the wings deploy and the servo turns them toward the lamp; the sun arrow in the 3D view locks onto the lamp direction; the sim reaction wheels spin up to "sun-point". Meanwhile the map shows the ISS-orbit sub-satellite point crossing the terminator, the STAR LED dims for eclipse, and a countdown reads "next pass over Tel Aviv in 47 min — beacon armed". All truthful, all on existing hardware.

## Questions for the user
1. **MPU9250 or MPU6500?** Run `readRegister(0x75)`: 0x71 vs 0x70. Decides ideas 4, 5 and yaw quality.
2. **IMU orientation on the PCB** relative to the four sun-sensor faces and the wings (which chip axis points to ph4/front, which to top?). Or accept a 6-position calibration wizard.
3. **Photoresistor wiring**: is the LDR high-side (brighter = lower ADC, as the GUI assumes) and are all four the same part? Needed for a calibration table.
4. **Servo mechanics**: can the wings sit at intermediate angles without stalling, and is the 2.2 s cutoff about heat or about the hard stop? Decides SADA (idea 8).
5. **Site and target orbit**: user's lat/lon for pass prediction, and do they want the real ISS TLE or a fictional MySat orbit? Is internet available for NTP/TLE refresh, or only via manual uplink?
