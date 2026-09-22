#!/usr/bin/env bash
# Compile-check the OBC (ESP32-CAM) firmware without PlatformIO, the way tools/build_aux.sh does
# for the Nano. It drives arduino-cli against a manually-placed Arduino-ESP32 core, so it works in
# environments where the PlatformIO registry and the Arduino board/library indexes are unreachable
# but GitHub release assets are.
#
# This is a *build check*: it produces .elf/.bin under .build/obc but does not flash anything. For
# day-to-day work with normal internet access, `pio run -e obc` is the supported path
# (docs/FLASHING.md) and does the same job with fewer moving parts.
#
#   tools/build_obc.sh                 # uses .build/toolchain, downloading what is missing
#   tools/build_obc.sh --fetch-only    # just populate .build/toolchain and stop
#
# Downloads on first run (~350 MB, cached in .build/toolchain afterwards):
#   arduino-cli, Arduino-ESP32 2.0.17, the xtensa-esp32-elf 8.4.0 toolchain, and the five
#   lib_deps libraries from their upstream GitHub repositories. `esptool` comes from PyPI.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TC="${MYSAT_TOOLCHAIN_DIR:-$ROOT/.build/toolchain}"
OUT="$ROOT/.build/obc"
ACLI_VER=1.3.1
CORE_VER=2.0.17
FQBN="esp32:esp32:esp32cam:PartitionScheme=huge_app,CPUFreq=240,FlashFreq=80,FlashMode=qio"
FW_VERSION="$(sed -n 's/.*MYSAT_FW_VERSION=\\"\([^\\]*\)\\".*/\1/p' "$ROOT/platformio.ini" | head -1)"
FW_VERSION="${FW_VERSION:-2.1.0-demo}"

mkdir -p "$TC/bin" "$TC/sketchbook/hardware/esp32" "$TC/sketchbook/libraries" "$TC/esptool_py" "$TC/ctags"

fetch() { # url dest
  [ -s "$2" ] && return 0
  echo "  fetching $(basename "$2")"
  curl -sSL --retry 4 --retry-delay 2 -o "$2" "$1"
}
clone() { # owner/repo tag dir
  local d="$TC/sketchbook/libraries/$3"
  [ -d "$d" ] && return 0
  echo "  cloning $3 $2"
  git clone -q --depth 1 --branch "$2" "https://github.com/$1.git" "$d" 2>/dev/null \
    || git clone -q --depth 1 "https://github.com/$1.git" "$d"
  rm -rf "$d/.git"
}

echo "== toolchain ($TC)"
if [ ! -x "$TC/bin/arduino-cli" ]; then
  fetch "https://github.com/arduino/arduino-cli/releases/download/v$ACLI_VER/arduino-cli_${ACLI_VER}_Linux_64bit.tar.gz" "$TC/acli.tar.gz"
  tar xzf "$TC/acli.tar.gz" -C "$TC/bin" arduino-cli
fi
if [ ! -d "$TC/esp32-$CORE_VER" ]; then
  fetch "https://github.com/espressif/arduino-esp32/releases/download/$CORE_VER/esp32-$CORE_VER.zip" "$TC/esp32.zip"
  unzip -q -o "$TC/esp32.zip" -d "$TC"
fi
if [ ! -d "$TC/xtensa-esp32-elf" ]; then
  fetch "https://github.com/espressif/crosstool-NG/releases/download/esp-2021r2-patch5/xtensa-esp32-elf-gcc8_4_0-esp-2021r2-patch5-linux-amd64.tar.gz" "$TC/xtensa.tar.gz"
  tar xzf "$TC/xtensa.tar.gz" -C "$TC"
fi
[ -e "$TC/sketchbook/hardware/esp32/esp32" ] || ln -s "$TC/esp32-$CORE_VER" "$TC/sketchbook/hardware/esp32/esp32"

# The Arduino library index host is often blocked where the GitHub release assets are not, so the
# lib_deps of platformio.ini are cloned from upstream at the same versions.
clone bblanchon/ArduinoJson      v6.21.3 ArduinoJson
clone adafruit/Adafruit_NeoPixel 1.12.3  Adafruit_NeoPixel
clone adafruit/Adafruit_ADS1X15  2.5.0   Adafruit_ADS1X15
clone adafruit/Adafruit_BusIO    1.16.1  Adafruit_BusIO
clone Makuna/Rtc                 2.4.3   RTC_by_Makuna
# ... and the two libraries this repo vendors itself.
for v in BSEC Beastdevices_INA3221; do
  rm -rf "$TC/sketchbook/libraries/$v"; cp -r "$ROOT/lib/$v" "$TC/sketchbook/libraries/$v"
done

# arduino-cli insists on a ctags binary to scan the .ino for prototypes. Ours is an empty shim
# (setup()/loop() live in main.cpp), so an empty tag list is the correct answer.
printf '#!/bin/sh\nexit 0\n' > "$TC/ctags/ctags"; chmod +x "$TC/ctags/ctags"

# esptool turns the .elf into a flashable image. The Arduino recipe hard-codes the name
# "esptool.py", which would shadow the installed module, hence the launcher.
python3 -c "import esptool" 2>/dev/null || python3 -m pip install -q "esptool>=4.6,<5"
cat > "$TC/esptool_py/esptool.py" <<'PY'
#!/usr/bin/env python3
import os, sys
_here = os.path.realpath(os.path.dirname(os.path.abspath(__file__)))
sys.path = [p for p in sys.path if os.path.realpath(p or os.getcwd()) != _here]
import esptool
sys.exit(esptool._main() if hasattr(esptool, "_main") else esptool.main())
PY
chmod +x "$TC/esptool_py/esptool.py"

cat > "$TC/arduino-cli.yaml" <<EOF
directories:
  data: $TC/acli_data
  downloads: $TC/acli_data/staging
  user: $TC/sketchbook
library:
  enable_unsafe_install: true
logging:
  level: error
EOF

[ "${1:-}" = "--fetch-only" ] && { echo "toolchain ready in $TC"; exit 0; }

# arduino-cli wants a sketch folder; src/obc plus the portable shared/ headers are assembled into
# one, and it compiles <sketch>/src recursively.
echo "== assembling sketch"
SK="$ROOT/.build/obc_sketch"
rm -rf "$SK" "$OUT"; mkdir -p "$SK/src" "$OUT"
printf '// Build shim: setup() and loop() live in src/main.cpp.\n' > "$SK/obc_sketch.ino"
cp -r "$ROOT/src/obc/." "$SK/src/"
cp "$ROOT"/shared/*.h "$SK/src/"

# The -I must point at arduino-cli's COPY of the sketch, not the original: reaching the same header
# through two paths makes #pragma once stop deduplicating and every struct redefines itself.
echo "== compiling ($FQBN)"
"$TC/bin/arduino-cli" --config-file "$TC/arduino-cli.yaml" compile \
  --fqbn "$FQBN" \
  --build-property "runtime.tools.xtensa-esp32-elf-gcc.path=$TC/xtensa-esp32-elf" \
  --build-property "runtime.tools.esptool_py.path=$TC/esptool_py" \
  --build-property "runtime.tools.ctags.path=$TC/ctags" \
  --build-property "compiler.cpp.extra_flags=-I$OUT/sketch/src -DMYSAT_FW_VERSION=\"$FW_VERSION\" -DMYSAT_TARGET_OBC=1" \
  --build-property "compiler.c.extra_flags=-I$OUT/sketch/src -DMYSAT_TARGET_OBC=1" \
  --build-path "$OUT" \
  "$SK"

echo "OK: $OUT/obc_sketch.ino.bin"
