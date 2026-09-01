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

The first `pio run` for a target downloads its toolchain and libraries — this needs real internet
access (this repo was built in a network-isolated sandbox where those downloads were blocked, so
`obc` in particular has not been compiled end-to-end yet; see the note at the bottom of
`docs/ARCHITECTURE.md`). Expect to fix a handful of real compiler errors on your first build —
that's normal for a from-scratch rewrite this size, and every third-party library call was checked
by hand against the actual vendored headers, so errors should be typos/API drift, not design
mistakes.

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

**Arduino Nano (`aux`)** flashes over its own USB port:

```
pio run -e aux -t upload
```

If upload fails with `"not in sync"` or similar, your board likely has the old bootloader; edit
`platformio.ini` and change `board = nanoatmega328new` to `board = nanoatmega328` under `[env:aux]`.

## Verifying without a board (what was actually run while building this)

```
tools/run_native_tests.sh        # compiles + runs the 20 tests in test/test_core with plain g++
tools/build_aux.sh <avr-core> <avr-servo>   # a real avr-gcc build of the Nano firmware
```

`run_native_tests.sh` has no dependencies beyond a working `g++`. `build_aux.sh` needs `gcc-avr` /
`avr-libc` (`apt-get install gcc-avr avr-libc binutils-avr` on Debian/Ubuntu) plus local clones of
two repos:

```
git clone --depth 1 --branch 1.8.6 https://github.com/arduino/ArduinoCore-avr.git /tmp/avr-core
git clone --depth 1 https://github.com/arduino-libraries/Servo.git /tmp/avr-servo
tools/build_aux.sh /tmp/avr-core /tmp/avr-servo
```

Both scripts exist specifically because the PlatformIO and Arduino package registries were
unreachable in the sandbox this project started in; once you have normal internet access, `pio run`
and `pio test` are the more convenient day-to-day commands and do the same thing (plus the ESP32
side, which these scripts don't cover).

## First boot checklist

1. `pio device monitor -e obc` and watch the boot banner — it prints the firmware version, boot
   count, and reset reason, then each sensor's init result (`BME680=1 IMU=1(who=0x71) ...`). The
   `who=` value tells you which IMU you have (`0x71` MPU9250 with magnetometer, `0x70` MPU6500).
2. `imu calibrate` — hold the satellite still for 3 seconds.
3. `status` — check every sensor reads something sane.
4. Join the `<callsign>-XXXX` WiFi access point it creates by default and browse to
   `http://192.168.4.1/` for the dashboard.
