# EPS & Thermal Council Input: MySat "real satellite" firmware

Source facts used: the INA3221 driver (CH1 battery, CH2/CH3 panels, 100 mΩ shunts, polled every 500 ms, no averaging configured), the Nano sketch (servo with 2.2 s power cutoff, HC-12 power on D5, one-way I2C), `camera_pins.h` (AI-Thinker `PWDN_GPIO_NUM = 32`, so camera power-down is real), and the loop/logger structure.

## Reality check first (applies to everything below)

**INA3221 with 100 mΩ shunts.** Shunt LSB is 40 µV, so current resolution is 0.4 mA and full scale is ±1.64 A; offset error is up to ~±80 µV (±0.8 mA) plus ~0.25 % gain. Bus voltage LSB is 8 mV. Two things the stock firmware gets wrong that any EPS feature depends on:
1. It reads a single unaveraged sample every 500 ms. WiFi TX bursts (200–400 mA for a few ms) are aliased. Fix: `ina.setAveragingMode(INA3221_REG_CONF_AVG_64)` and 1.1 ms conversion time, so each reading is a ~0.4 s true mean. Zero firmware cost, big honesty gain.
2. VBUS is measured at IN-, i.e. after the shunt. At 500 mA discharge the "battery voltage" reads 50 mV low. Correct with `V_true = V_bus + I*0.1` (sign per topology). 50 mV is ~10 % SoC in the flat part of a Li-ion curve, so this matters.

Coulomb counting drift with ±1 mA offset is about ±24 mAh/day: fine against a 1000–2600 mAh cell if re-anchored to OCV daily. SoC from OCV alone is ±5–8 % in the 3.7–3.9 V plateau. Per-subsystem power cannot be measured directly (one battery shunt); it must be characterized once and attributed by state, which is exactly how flight EPS budgets are built.

## Feature ideas

**1. Battery SoC estimator (OCV + coulomb counting)**
Maintains SoC in %, mAh remaining, and time-to-empty. Coulomb counts CH1 current at 2 Hz (`mAh -= I*dt/3600`), re-anchors to an OCV table when |I| < 20 mA for 10 min or at rest after boot, persists mAh, cycle count and last anchor in NVS every 5 min and on the existing heartbeat. Real sats do this because voltage alone is meaningless under load. Implement: 11-point OCV table (3.0…4.2 V, chemistry-dependent), rate-limited anchoring, IR-compensated voltage from point 2 above. Coolness 4, effort M, hardware none. Gotchas: needs capacity and chemistry (LiPo vs LiFePO4 curves differ completely); sign convention of CH1 must be verified with a charger connected.

**2. Energy budget accounting**
Integrates P_gen = V_panel·(I_L+I_R), P_bat_in and P_bat_out into hourly buckets (24-entry ring in NVS) and per "orbit" (idea 5). Reports Wh generated vs consumed, net balance, and "orbits until empty at current deficit". Real sats fly with a positive-margin power budget and operators watch it every pass. Implement as a `PowerBudget` struct updated in the 500 ms loop, exposed in `/get_data`, a new CSV column set, and a console `PowerReport` command. Coolness 4, effort M, none. Gotcha: charger efficiency is unknown, so report panel-side and battery-side energy separately rather than a fake "load Wh".

**3. Subsystem power characterization ("bench test") and attribution**
A one-shot `CharacterizePower` routine toggles each load in sequence (STAR LED, camera deinit+PWDN, WiFi off/on, modem sleep, CPU 80/160/240 MHz, HC-12 via Nano) for 5 s each, records the battery-current delta, and stores a table in NVS. Afterwards the firmware attributes live consumption to subsystems from their known states and shows a pie/bar per subsystem. This is how real budgets are derived (measured deltas, not datasheets). Coolness 4, effort M, none. Gotcha: deltas of <5 mA are within noise; report an uncertainty band.

**4. Energy-based power modes with hysteresis (measurable)**
NOMINAL / POWER-SAVE / SURVIVAL / CRITICAL driven by SoC (or IR-corrected voltage as fallback): enter power-save below 40 %, survival below 20 %, critical below 10 %; exit only 10 % higher. Actions that really cut current on this board: `WiFi.setSleep(true)` (modem sleep, ~20–30 mA saved, GUI still works), `setCpuFrequencyMhz(80)` (~20 mA), `esp_camera_deinit()` then `digitalWrite(32, HIGH)` (~20–40 mA), NeoPixel dimmed, telemetry 1.5 s → 10 s → 60 s, camera capture forbidden, HC-12 power via Nano D5 in critical mode. SURVIVAL uses timer light sleep (`esp_sleep_enable_timer_wakeup`, `esp_light_sleep_start`) in 1 s naps between telemetry beats; expect ~150 mA → ~60 mA → ~25 mA, readable on the INA3221 itself. Coolness 5, effort L, none. Gotchas: light sleep kills the WebServer and drops the first UART chars, so in survival the "ground station" is the serial/HC-12 link only, which is realistic. Deep sleep will not go below ~8–10 mA on an ESP32-CAM because of the LDO, PSRAM and camera rails. The Nano (~20 mA) is untouchable from firmware.

**5. Eclipse detection and orbit statistics**
Declares ECLIPSE when I_L+I_R < 5 mA and the sum of the four photoresistors drops below a learned threshold for >3 s, SUNLIT on the reverse with hysteresis. Tracks eclipse count, sunlit/eclipse durations, longest eclipse, mean generation per sunlit phase, and derives a synthetic "orbit period". Real sats use this for power planning and for sanity-checking the orbit propagator. Implement as a small state machine; optional `SimOrbit 90 35` mode fakes a 90 min orbit with 35 min eclipse to exercise the budget logic indoors overnight (clearly labeled SIM in telemetry). Coolness 4, effort S, none. Gotcha: under indoor LED light panel current may be only a few mA; use photoresistors as the primary sensor.

**6. Solar array health monitor**
Per-panel I-V snapshot (V from CH2/CH3 bus, I from shunt), daily peak power estimate (Wp under the current illumination), and a left/right asymmetry ratio `A = (I_L−I_R)/(I_L+I_R)`. |A| > 0.3 for >30 s while sunlit raises a PANEL_SHADOW/DEGRADATION event; a long-term ratio trend in NVS flags a real degradation. Real sats track string currents exactly this way. Coolness 3, effort S, none. Gotchas: stop averaging the two bus voltages as the stock code does; if the panels are paralleled after the shunts both bus voltages are identical and only current carries information.

**7. Realistic deployment sequence with EPS go/no-go and confirmation**
`Separation` (or a boot flag) starts an inhibit timer (real: 30 min; demo: 30 s), then deployment requires V_batt > 3.7 V, no eclipse, and `stateMotor == closed`. Confirmation is telemetry-based, not command-based: compare the 5 s mean of I_L+I_R and the accelerometer RMS before and after; a current jump plus a vibration signature within the 2.2 s window equals DEPLOY_CONFIRMED, otherwise DEPLOY_ANOMALY with automatic retry limit (3). Persist attempts in NVS. Coolness 5, effort M, none. Gotcha: folded panels may already face the light; if the pre/post current change is small, fall back to photoresistor change and accelerometer jolt.

**8. Peak-power array positioning (solar array drive)**
New Nano command `SET_ANGLE n` (0x10+angle byte) moves the wing hinge to an arbitrary angle, powering the servo only while moving and detaching after arrival or 2.2 s. The ESP32 runs a slow hill-climb every 60 s: step ±5°, keep the direction that increased I_L+I_R, hold otherwise. This is a genuine one-axis SADA/MPPT-like behavior. Coolness 4, effort M, none. Gotchas: the 2.2 s cutoff exists because the servo stalls at the end stops; verify intermediate angles do not stall and that the detached servo holds position under gravity. Limit to a few moves per hour to protect the mechanism.

**9. Battery protection and health**
Firmware-side protection: load shed at 3.5 V loaded / 3.3 V rest (deep sleep with 10 min wake-check), charge-state classifier from sign of I and dV/dt (CC, CV, FULL when I < C/20 at 4.15 V, DISCHARGE), over-temperature guard using BME680 > 45 °C to drop to power-save, equivalent-full-cycle counter (`cycles += discharged_mAh / capacity`), and a `CapacityTest` that measures mAh delivered between two OCV anchors. Coolness 3, effort M, none. Honesty: cutting the cell is the charger/protection IC's job; firmware can only reduce load. Capacity fade on a hobby kit is invisible for months; present it as "measured capacity" with an error bar.

**10. Thermal telemetry, zones and limits**
Four real temperature sources: BME680 (±1 °C, "structure"), DS3231 register 0x11 (0.25 °C res, ±3 °C, "EPS board"), MPU TEMP_OUT (`raw/333.87+21`, "ADCS"), ESP32 `temperatureRead()` ("OBC", trend only, absolute value is poor). Yellow/red limits per zone, thermal event log, and OBC-minus-ambient gradient as a dissipation proxy that visibly rises when WiFi and camera are on. Coolness 3, effort S, none.

**11. Heater loop (partly simulated)**
Thermostat on the STAR LED PWM: below setpoint, duty rises; telemetry shows heater duty, heater mA and cumulative heater Wh. The current draw is real (~20 mA), the temperature effect is not: a white LED will not warm the BME680 measurably. Say so in the GUI ("simulated thermal plant") or add the optional hardware: a 10 Ω 2 W resistor on a Nano pin via MOSFET, taped to the BME680, gives a genuine closed loop. Coolness 3, effort S, optional resistor+MOSFET.

**12. Nano as independent EPS microcontroller**
Add `Wire.onRequest` so the ESP32 can read a 16-byte EPS frame (Nano uptime, battery ADC, servo angle, RF state, watchdog counter). Wire battery through a 2×100 k divider to A0 (one resistor pair; Nano ADC is 10-bit ≈ 5 mV, fine for protection thresholds). The Nano then sheds the HC-12 by itself below 3.4 V regardless of ESP32 health, and implements an external watchdog: if the ESP32 stops sending an I2C heartbeat for 3 min, pulse a free pin wired to the ESP32 EN pad. Real EPS boards do exactly this (independent under-voltage lockout and OBC reset). Coolness 5, effort M, optional: two resistors and one wire to EN. Gotcha: confirm D5 polarity for HC-12 power on the user's board revision and whether the Nano runs at 5 V or 3.3 V.

**Visualization.** Live power-flow diagram in the GUI (panels → charger → battery → loads, arrow width = mA), SoC gauge with mode band, 24 h generated/consumed bar chart from NVS buckets, orbit timeline with eclipse shading, thermal zone strip with limits. Same data as compact CSV over Serial for the HC-12 ground terminal.

## Flagship 60-second demo: "Eclipse entry and power-save"

GUI shows the power-flow diagram, SoC and mode. A visitor covers both wings with their hands. Within 3 s the sat logs `ECLIPSE_ENTRY`, the NeoPixel turns amber, generation arrows collapse to zero and "time to empty" appears. You type `SimSoc 35` on the console; the sat announces `MODE POWER-SAVE`, the camera is powered down, CPU drops to 80 MHz, WiFi enters modem sleep, and the visitor watches the battery current bar fall from ~150 mA to ~60 mA on the same screen, with the subsystem attribution bars shrinking one by one. Hands away: `ECLIPSE_EXIT`, panels back, SoC climbs, mode returns to NOMINAL after hysteresis. Everything shown is real measurement except the injected SoC, which is labeled SIM.

## Questions for the user

1. Battery chemistry, cell count and capacity (LiPo/Li-ion/LiFePO4, mAh, protection circuit present?). The OCV table and all thresholds depend on it.
2. Which charger IC and topology: is there a boost to 5 V, and does USB power flow through the INA3221 CH1 shunt (does charging appear as negative battery current)?
3. Solar panel specs (Voc, Isc, Wp) and whether left/right are paralleled before or after the INA3221 channels.
4. Does the servo stall at intermediate angles, and does the detached servo hold the wings against gravity? (Decides idea 8.)
5. Nano supply rail (5 V or 3.3 V), HC-12 D5 polarity on your board revision, and willingness to add two resistors and one wire (Nano → ESP32 EN) for idea 12.
