#!/bin/sh
# Everything that runs without the FM-1: the simulator's scenarios (the firmware's UI, song, voice and
# engine from scripts), the companion's import of the whole song library through the firmware's own
# engine in WebAssembly, the storage and libc checks, the speech regression; then, unless --host-only,
# the firmware package's update path.
#   tests/run_tests.sh [--host-only]
set -eu
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
CC=${CC:-clang}
case "${1:-}" in ""|--host-only) ;; *) echo "usage: $0 [--host-only]" >&2; exit 2;; esac
"$PY" tools/build_host.py
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
for s in tests/scenarios/*.sim; do
    name=$(basename "$s" .sim)
    mkdir -p "build/sim/$name"
    build/host/sim "$s" "build/sim/$name"
    mkdir -p "build/sim/$name-quick"
    SIM_HOLD=30 build/host/sim "$s" "build/sim/$name-quick" >/dev/null   # quick taps (30 ms), as a fast hand
done
"$PY" tools/build_web.py
node tests/import.mjs
"$CC" -g -O1 -fno-builtin -fsanitize=address,undefined tests/libc_strings.c -o build/host/libc_strings
build/host/libc_strings
"$CC" -O2 tests/storage_test.c -o build/host/storage_test
build/host/storage_test
"$PY" tests/klatt_regression.py
if [ "${1:-}" = --host-only ]; then exit 0; fi
"$CC" -O2 tests/ota_test.c -o build/host/ota_test
build/host/ota_test build/386dx.fwsc
"$CC" -O2 tests/ldr_test.c -o build/host/ldr_test
"$PY" tools/test_package.py
