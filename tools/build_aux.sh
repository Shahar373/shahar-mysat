#!/usr/bin/env bash
# Manual avr-gcc build of the AUX (Nano) firmware, independent of PlatformIO/Arduino IDE network
# access. Used in CI/offline environments to prove src/aux/main.cpp actually compiles and links
# against a real ArduinoCore-avr + Servo library. For day-to-day development, PlatformIO
# (`pio run -e aux`) is more convenient once you have network access; this script exists because
# the PlatformIO and Arduino package registries were unreachable from the sandbox this project was
# bootstrapped in.
#
# Usage: tools/build_aux.sh [path-to-ArduinoCore-avr] [path-to-Servo-library]
#   Clone sources once:
#     git clone --depth 1 --branch 1.8.6 https://github.com/arduino/ArduinoCore-avr.git /tmp/avr-core
#     git clone --depth 1 https://github.com/arduino-libraries/Servo.git /tmp/avr-servo
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AVRCORE="${1:-/tmp/avr-core}"
AVRSERVO="${2:-/tmp/avr-servo}"
BUILD="$ROOT/.build/aux"
MCU=atmega328p
FCPU=16000000L

command -v avr-gcc >/dev/null || { echo "avr-gcc not found (apt-get install gcc-avr avr-libc binutils-avr)"; exit 1; }
[ -d "$AVRCORE/cores/arduino" ] || { echo "ArduinoCore-avr not found at $AVRCORE"; exit 1; }
[ -f "$AVRSERVO/src/avr/Servo.cpp" ] || { echo "Servo library not found at $AVRSERVO"; exit 1; }

rm -rf "$BUILD" && mkdir -p "$BUILD"
INC="-I$AVRCORE/cores/arduino -I$AVRCORE/variants/eightanaloginputs -I$AVRCORE/libraries/Wire/src -I$AVRCORE/libraries/Wire/src/utility -I$AVRSERVO/src -I$AVRSERVO/src/avr -I$ROOT/shared"
COMMON="-w -Os -ffunction-sections -fdata-sections -MMD -flto -mmcu=$MCU -DF_CPU=$FCPU -DARDUINO=10819 -DARDUINO_AVR_NANO -DARDUINO_ARCH_AVR $INC"
CXXFLAGS="-c -g -std=gnu++11 -fno-fat-lto-objects $COMMON"
CFLAGS="-c -g -std=gnu11 $COMMON"

n=0
compile() {
  local src="$1" lang="$2"; n=$((n + 1)); local obj="$BUILD/obj_$n.o"
  if [ "$lang" = cpp ]; then avr-g++ $CXXFLAGS "$src" -o "$obj"; else avr-gcc $CFLAGS "$src" -o "$obj"; fi
}

for f in "$AVRCORE"/cores/arduino/*.cpp; do compile "$f" cpp; done
for f in "$AVRCORE"/cores/arduino/*.c; do compile "$f" c; done
compile "$AVRCORE/libraries/Wire/src/Wire.cpp" cpp
compile "$AVRCORE/libraries/Wire/src/utility/twi.c" c
compile "$AVRSERVO/src/avr/Servo.cpp" cpp
compile "$ROOT/src/aux/main.cpp" cpp

avr-gcc -w -Os -flto -fuse-linker-plugin -Wl,--gc-sections -mmcu=$MCU -o "$BUILD/aux.elf" "$BUILD"/*.o -lm
avr-size "$BUILD/aux.elf"
echo "OK: $BUILD/aux.elf ($n objects)"
