#!/usr/bin/env bash
#
# gfwx/mayhem/build.sh — build kalcutter/gfwx's GFWX decoder harness as a sanitized libFuzzer
# target (+ a standalone reproducer), and a golden round-trip test binary for mayhem/test.sh.
#
# gfwx is a single C++20 header (gfwx.h) — no library to compile separately; the codec is template
# code instantiated inside each harness, so $SANITIZER_FLAGS on the harness compile instruments the
# whole codec automatically.
#
# Fuzzed surface (gfwx_decode_fuzzer): GFWX::decompress() on attacker-controlled GFWX bitstreams
# (magic 'GFWX'). Input IS a raw .gfwx file. See harnesses/gfwx_decode_fuzzer.cpp for the contract.
#
# Build contract comes from the org base ENV (CC/CXX/SANITIZER_FLAGS/LIB_FUZZING_ENGINE/SRC).
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' — must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# `=` (not `:=`) for SANITIZER_FLAGS so an explicit empty --build-arg builds with NO sanitizers.
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE

cd "$SRC"

HARNESS_DIR="$SRC/mayhem/harnesses"
INC="-I$SRC"                 # gfwx.h lives at the repo root
CXXSTD="-std=c++20"
BUILD="$SRC/mayhem-build"
mkdir -p "$BUILD"

# Standalone run-once driver (reads one input file, calls LLVMFuzzerTestOneInput). Plain C; compile
# as a C object so it links with the C++ harness object (the harness declares the entry extern "C").
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$HARNESS_DIR/standalone_main.c" -o "$BUILD/standalone_main.o"

# ── Build the decoder harness twice: libFuzzer target + standalone reproducer ──────────────────────
HARNESS=gfwx_decode_fuzzer

# libFuzzer target -> /mayhem/<name>
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $CXXSTD $INC \
    "$HARNESS_DIR/$HARNESS.cpp" $LIB_FUZZING_ENGINE \
    -o "/mayhem/$HARNESS"

# standalone reproducer (no libFuzzer runtime) -> /mayhem/<name>-standalone
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $CXXSTD $INC \
    "$HARNESS_DIR/$HARNESS.cpp" "$BUILD/standalone_main.o" \
    -o "/mayhem/$HARNESS-standalone"

echo "built $HARNESS (+ standalone)"

# ── Build the golden round-trip test with NORMAL flags (no sanitizers) so test.sh only RUNS it.
#    The test program encodes a known image with GFWX::compress, decodes it back at QualityMax
#    (lossless), and asserts byte-exact pixel recovery — a real known-answer oracle. ──────────────────
env -u CFLAGS -u CXXFLAGS -u SANITIZER_FLAGS \
  $CXX -O2 $CXXSTD $INC "$HARNESS_DIR/gfwx_roundtrip_test.cpp" -o "$BUILD/gfwx_roundtrip_test"
echo "built gfwx_roundtrip_test in mayhem-build/"

echo "build.sh complete:"
ls -la "/mayhem/$HARNESS" "/mayhem/$HARNESS-standalone" "$BUILD/gfwx_roundtrip_test" 2>&1 || true
