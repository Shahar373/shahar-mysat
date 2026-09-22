# Building & flashing

## Install PlatformIO

Easiest: install the "PlatformIO IDE" extension in VS Code, then open this repo's folder — it
picks up `platformio.ini` automatically. Command-line alternative: `pip install platformio`.

## Build

```
pio run -e obc        # ESP32-CAM main firmware
pio run -e aux         # Arduino Nano auxiliary firmware
pio test -e native     # portable core logic, runs on your machine, no board needed
```

The first `pio run` for a target downloads its toolchain and libraries, which needs real internet
access. All three targets do build: `obc` links at about 1.06 MB of the 3 MB app slot against
Arduino-ESP32 2.0.17 — see the bottom of `docs/ARCHITECTURE.md` for how that is verified, and
`tools/build_obc.sh` below if the PlatformIO registry is blocked where you are.

## Flash

**ESP32-CAM (`obc`)** needs an FTDI/CP2102 USB-serial adapter (no onboard USB port) and the usual
"hold IO0 low, press reset, release IO0" boot sequence — same as the stock firmware's setup.

```
pio run -e obc -t upload
pio run -e obc -t uploadfs      # uploads data/index.html to LittleFS — do this once, and again
                                 # whenever you edit data/index.html. Skip it and the board falls
                                 # back to the compact built-in page, which still works.
pio device monitor -e obc
```

**Arduino Nano (`aux`)** flashes over its own USB port, and is optional at first -- see "Which
Nano firmware you have" below:

```
pio run -e aux -t upload
```

If upload fails with `"not in sync"` or similar, your board likely has the old bootloader; edit
`platformio.ini` and change `board = nanoatmega328new` to `board = nanoatmega328` under `[env:aux]`.

## Verifying without a board (what was actually run while building this)

```
tools/run_native_tests.sh        # compiles + runs the 52 tests in test/test_core with plain g++
tools/build_aux.sh <avr-core> <avr-servo>   # a real avr-gcc build of the Nano firmware
tools/build_obc.sh               # a real arduino-cli build of the ESP32-CAM firmware
```

`run_native_tests.sh` has no dependencies beyond a working `g++`. `build_aux.sh` needs `gcc-avr` /
`avr-libc` (`apt-get install gcc-avr avr-libc binutils-avr` on Debian/Ubuntu) plus local clones of
two repos:

```
git clone --depth 1 --branch 1.8.6 https://github.com/arduino/ArduinoCore-avr.git /tmp/avr-core
git clone --depth 1 https://github.com/arduino-libraries/Servo.git /tmp/avr-servo
tools/build_aux.sh /tmp/avr-core /tmp/avr-servo
```

`build_obc.sh` fetches what it needs itself (arduino-cli, Arduino-ESP32 2.0.17, the xtensa
toolchain, and the `lib_deps` libraries from their upstream GitHub repositories — about 350 MB,
cached in `.build/toolchain` afterwards) and then compiles in roughly 30 seconds:

```
tools/build_obc.sh               # -> .build/obc/obc_sketch.ino.bin
tools/build_obc.sh --fetch-only  # just populate .build/toolchain
```

All three scripts exist because the PlatformIO and Arduino package registries are unreachable in
some environments, including the sandbox this project started in; they pull only from GitHub
release assets and PyPI. With normal internet access `pio run` and `pio test` are the more
convenient day-to-day commands and do the same thing.

## Which Nano firmware you have matters less than it used to

The OBC probes the Nano at boot. If it answers a status read it is the v2 firmware from `src/aux/`
and the full protocol is used. If it does not, it is the firmware the kit ships with, and the OBC
switches to that firmware's single-byte commands: wings open and close still work, arbitrary
angles, the heartbeat and the position readback do not, and the console says
`stock firmware, no readback`. That means you can flash the ESP32-CAM first and see everything
work before touching the Nano. Flash the Nano when convenient; the OBC notices on the next boot.

The one thing the OBC never does is send its framed commands to the stock firmware: that firmware
keeps only the last byte of a message, which for a frame is its checksum, and for two checksum
values it would read that as a wing command.

## Running the demonstration show instead

If what you want on the bench is the show — wings out and back twice, then the front light on for
three seconds three times — arm it once:

```
demo on
```

That is stored in NVS and survives reflashing. From then on, pulling the launch pin runs the show
instead of the deployment sequence below, and the same warnings apply, more so: the show moves the
servo **four times** rather than once, so the wings must be free before you power up. `demo off`
goes back to the normal single deployment, `demo run` runs the show on demand without a power
cycle, and `demo` prints the schedule. Full reference in `docs/COMMANDS.md`.

## Before the first power-up: the servo will move

The mission sequencer treats a power-on reset as the launch pin being pulled, and deploys the
panels `mission.deploy_inhibit_s` seconds later (10 s by default). Two consequences on the bench:

- Make sure the wings are free to move before you power up, or the servo will push against a
  restrained mechanism for 2.2 s (the AUX controller cuts its power at that point, so it survives,
  but there is no reason to do it repeatedly).
- On ESP32 an upload-triggered reset usually reports as a power-on too, so **the countdown starts
  again after every firmware upload.** While you are iterating on code, turn the sequence off once:

```
params set mission.auto_deploy 0
```

That is stored in NVS and survives reflashing. Turn it back on with `params set
mission.auto_deploy 1`, or leave it off and run the whole sequence on demand with
`mission separate`. You can also abort a countdown already in progress by typing `mission abort`
within those 10 seconds.

The boot banner prints the reset reason it saw (`POWERON`, `SOFTWARE`, `TASK_WDT`, ...). If after
an upload it reports something other than `POWERON`, the automatic sequence will not have run --
pull the power pin for a genuine cold start, or use `mission separate`.

A step-by-step walkthrough of all of the above for a first-time flasher is in `docs/INSTALL.md`
(English) and `docs/INSTALL.he.md` (Hebrew).

## First boot checklist

1. `pio device monitor -e obc` and watch the boot banner — it prints the firmware version, boot
   count, and reset reason, then each sensor's init result (`BME680=1 IMU=1(who=0x71) ...`). The
   `who=` value tells you which IMU you have (`0x71` MPU9250 with magnetometer, `0x70` MPU6500).
2. `imu calibrate` — hold the satellite still for 3 seconds.
3. `status` — check every sensor reads something sane.
4. Join the `<callsign>-XXXX` WiFi access point it creates by default and browse to
   `http://192.168.4.1/` for the dashboard.
5. `mission status` — it should report a phase, and `no upright reference yet` until the satellite
   has sat still for three seconds. Once it has, `mission status` shows the learned vector.
6. Test the flip behaviour: with the panels deployed, turn the satellite upside down and hold it
   there. After two seconds the panels fold and the SIGNAL LED turns magenta. Turn it back upright
   and they deploy again.
7. Optional: `demo run` to watch the demonstration show once with the wings free, before arming it
   on the launch pin with `demo on`.

## Upgrading from the previous firmware

The parameter table gained the `demo.*` fields, so it is version 3 where the previous firmware
wrote version 2. It is upgraded in place on the first boot: your callsign, WiFi credentials, gyro
calibration and learned upright reference are kept, and the new fields take their defaults. The
event log records `params upgraded v2 -> v3, settings kept`. A table that fails its CRC still falls
back to the golden copy and then to defaults, exactly as before.
