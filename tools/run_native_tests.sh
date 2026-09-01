#!/usr/bin/env bash
# Compiles and runs test/test_core/test_core.cpp against the portable shared/ headers using the
# system g++ and a small Unity-compatible shim (test/unity_shim), so this works fully offline.
# With network access, prefer `pio test -e native` (uses the real Unity framework instead).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/.build"
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter \
  -I "$ROOT/shared" -I "$ROOT/test/unity_shim" \
  "$ROOT/test/test_core/test_core.cpp" \
  -o "$ROOT/.build/test_core"
"$ROOT/.build/test_core"
