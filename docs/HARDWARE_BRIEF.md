# MySat Kit — Hardware & Firmware Brief (derived from reading MySatKit-Firmware v1.4.1 source)

Original firmware source is cloned at:
  /tmp/claude-0/-home-user-shahar-mysat/400d6ad1-7e76-5658-b010-b05d5a1ad627/scratchpad/MySatKit-Firmware/ino/
  - MySat_main/           (ESP32-CAM main OBC firmware, Arduino IDE, ~2500 lines incl. embedded HTML)
  - MySat_Nano_ATmega328p/ (Arduino Nano auxiliary MCU firmware)
Read any file there if you need exact details.

## Compute
- **Main OBC: ESP32-CAM (AI Thinker)** — dual-core 240 MHz, 520 KB SRAM + 4 MB PSRAM, 4 MB flash.
  WiFi (STA mode used today) + Bluetooth/BLE (unused). LittleFS partition for files (photos, CSV logs, config).
  Arduino IDE 2.x framework. Libraries: ArduinoJson, WebServer, LittleFS, Preferences (NVS), Adafruit_NeoPixel,
  Adafruit_ADS1X15, BSEC (Bosch IAQ), Beastdevices INA3221, Rtc_by_Makuna, esp_camera.
- **Auxiliary MCU: Arduino Nano (ATmega328P)** — I2C slave at address 0x08. Receives 1-byte commands from ESP32:
  0=MOTOR_OPEN, 1=MOTOR_CLOSE, 2=RF_TURN(no-op), 3=RF_SET(no-op in v1.3.0, intended to pull HC-12 SET pin).
  Controls: servo on D9 (solar panel deploy: 170°=closed → 10°=open, stepped 1°/10ms, power cut after 2.2 s to avoid
  overheating), HC-12 power pin D5, HC-12 SET pin D4, built-in LED pattern. Many Nano pins are FREE (D2,D3,D6,D7,D8,D10–D13,A0–A3).
  The Nano has NO telemetry path back to the ESP32 today (one-way I2C writes only) — could be extended (Wire.onRequest).

## ESP32-CAM GPIO budget (IMPORTANT constraint)
The OV2640 camera consumes most GPIOs. Used by MySat: GPIO15=SDA, GPIO13=SCL (I2C bus), GPIO14=STAR LED (PWM),
GPIO2=NeoPixel SIGNAL LED, GPIO1/3 = UART0 (USB serial AND, most likely, the HC-12 radio share this UART — the
firmware's "SetRadio" command just tells the Nano to pull SET and then AT commands are typed into the same Serial).
GPIO4 = on-board flash LED (unused by MySat), GPIO12 free-ish (strapping pin), GPIO16 used by PSRAM.
=> Practically NO free GPIOs on the ESP32. New hardware must hang off the I2C bus or the Nano.
=> The micro-SD slot conflicts with I2C/LED pins; assume NO SD card. Storage = LittleFS in flash (~1–2 MB usable).

## Sensors (all I2C on ESP32, Wire @ SDA15/SCL13)
- **BME680** @0x77 — temperature, humidity, pressure, gas resistance; BSEC library computes IAQ (needs ~5 min warm-up;
  state saved to NVS hourly). "Environment / atmospheric science payload".
- **MPU9250 or MPU6500** @0x69 — 3-axis accel + 3-axis gyro. Firmware reads raw registers, complementary filter →
  roll/pitch/yaw (yaw drifts, gyro-only). Gyro bias calibration saved to /cal.dat.
  If it is an MPU9250 it ALSO contains an AK8963 magnetometer (via I2C bypass) — unused today. Unknown which one the user has.
- **ADS1015** @0x48 — 12-bit 4-channel ADC reading 4 photoresistors: ph1=left, ph2=back, ph3=right, ph4=front
  ("sun sensors" on 4 side faces). Values ~0–2047.
- **INA3221** @0x40 — 3-channel V/I monitor, 100 mΩ shunts: CH1=battery (V, mA), CH2=left solar panel, CH3=right solar panel.
  Real solar panels on the deployable wings, real Li-ion/LiPo battery (voltage typically ~3.5–4.2 V range).
- **DS3231** RTC — battery-backed real time clock; time set manually via serial prompts today.
- **OV2640 camera** — JPEG capture, XGA (1024x768) quality 15 today, stored in LittleFS (ring of 10 photos + JSON index),
  served to Web GUI as base64.

## Actuators / indicators
- Servo (via Nano) — deploys/retracts the two solar wings. Binary open/close today; angle could be commanded continuously
  in a new Nano firmware but the 2.2 s power cutoff exists because the servo stalls against the mechanism (verify with user).
- STAR LED (GPIO14, PWM-dimmable white LED) — "payload light" / could act as a fake heater load or Morse beacon light.
- SIGNAL LED (NeoPixel RGB, GPIO2) — system status (blue blink = no WiFi, solid blue = WiFi OK today).
- Nano built-in LED.

## Radio
- **HC-12** 433 MHz serial transceiver (SI4463), default 9600 baud, supports up to 115200 baud, 100 channels, ~1 km LOS.
  Firmware runs Serial at 115200. Transparent serial pipe: whatever the ESP32 prints on Serial goes over the air to a
  second HC-12 on the ground (user may or may not own a second HC-12 — ask). Half-duplex, no framing, no CRC, no addressing.
  Note: because HC-12 shares UART0 with USB, the "console" IS the radio link. Ground station receives the same text.

## Existing firmware features (v1.4.1) — the baseline to beat
- Serial console commands (case-insensitive text): ChangeTime, SetWIFI, TurnLed, SolarDeploy, SolarRetract, SolarMove,
  Calibrate, TurnConsole, SetCallSign, SwitchTelemetry (text/plotter), SelectPlotterMode, DebugModeOn/Off, SetRadio,
  StartLogging(period), StopLogging, DeleteLogging, ListLogFiles, AuditFileSystem, BlinkLed, SendEventLog.
- Telemetry: human-readable text frame every 1.5 s with frame counter, or Arduino Serial Plotter CSV mode.
- Web GUI (Bootstrap, served from ESP32 over WiFi STA): live sensor widgets (env, attitude, sun tracker X/Y, battery, panels),
  photo capture + gallery of 10, LED/motor toggles, CSV log download, event log download. Endpoints: /get_data (JSON),
  /get_photo, /get_photo_list, /get_photo_by_id, /light_on, /motor_on, /get_log_list, /download_log, /download_event_log.
- Mission data logger: CSV rows to LittleFS, hourly file rotation, cap 3600 rows total, oldest-file overwrite.
- Event log: 100-line ring (BOOT, SHUTDOWN (reconstructed from NVS heartbeat), PHOTO n, COMMAND_RECEIVED).
- No: modes/state machine, watchdog, FDIR, binary protocol, CRC, command auth, ACKs, beacon, scheduler/time-tagged cmds,
  orbit model, attitude determination beyond tilt, SoC estimation, power budgeting, OTA, AP fallback, NTP, sleep modes,
  magnetometer, any use of the Nano's free pins, any use of BLE, any onboard image processing, any ground-station software.

## Who the user is
Hebrew-speaking hobbyist/student who assembled the kit and ran the stock firmware. Wants NEW firmware with cool features
that "behave like a real satellite". Skill level unknown (assume comfortable with Arduino, can learn). Budget for extra
hardware unknown — prefer firmware-only ideas, mark optional hardware add-ons clearly.
