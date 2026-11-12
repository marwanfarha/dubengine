#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The browser emulator built with plain clang and WASI libc, no Emscripten needed.
# Same sources and flags as web/emu/build.sh. Needs clang with the wasm32 target and a WASI sysroot
# (Debian/Ubuntu: apt install wasi-libc libclang-rt-dev-wasm32, or set WASI_SYSROOT).
#   web/emu/build_clang.sh  ->  build/emu/{index.html, worklet.js, amb.wasm}
set -e
cd "$(dirname "$0")/../.."
CLANG="${CLANG:-clang}"
SYSROOT="${WASI_SYSROOT:-/usr}"
mkdir -p build/gen build/emu
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
python3 tools/gen_words.py firmware/words.tsv web/words build/gen/ann_words.h >/dev/null
"$CLANG" --target=wasm32-wasi --sysroot="$SYSROOT" -isystem "$SYSROOT/include/wasm32-wasi" -L"$SYSROOT/lib/wasm32-wasi" \
    -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DEN_HOST -DEN_WEB "-DEN_VERSION=\"$(printf %s "${EN_VERSION:-DEV}" | tr a-z A-Z)\"" \
    -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen \
    -mexec-model=reactor -Wl,--export-dynamic -Wl,-z,stack-size=1048576 -Wl,--initial-memory=33554432 \
    -o build/emu/amb.wasm web/emu/amb_web.c firmware/src/dsp/amb.c -lm
cp web/emu/index.html web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/amb.wasm) B wasm)"
