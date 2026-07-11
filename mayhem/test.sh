#!/usr/bin/env bash
#
# gfwx/mayhem/test.sh — RUN the pre-built golden round-trip test (built by mayhem/build.sh with
# normal flags) and emit a CTRF summary. exit 0 iff no check failed.
#
# PATCH-grade oracle: gfwx_roundtrip_test encodes a known image with GFWX::compress at QualityMax and
# asserts BYTE-EXACT lossless recovery via GFWX::decompress, plus the GFWX magic and round-tripped
# header geometry. A no-op / exit(0) patch (or any change that breaks the compress↔decompress
# contract) cannot pass. This script only RUNS the pre-built binary; it never compiles.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

BIN="$SRC/mayhem-build/gfwx_roundtrip_test"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

if [ ! -x "$BIN" ]; then
  echo "missing $BIN — run mayhem/build.sh first" >&2
  emit_ctrf "gfwx-roundtrip" 0 1 0; exit 2
fi

echo "=== running gfwx round-trip golden test ==="
out="$("$BIN" 2>&1)"; rc=$?
echo "$out"

# The binary prints one 'ok:' / 'FAIL:' line per check. Count them for an accurate CTRF summary.
PASSED=$(printf '%s\n' "$out" | grep -c '^ok:')
FAILED=$(printf '%s\n' "$out" | grep -c '^FAIL:')
: "${PASSED:=0}" "${FAILED:=0}"

# If we could not parse any check lines, fall back to the binary's exit code as the verdict.
if [ "$(( PASSED + FAILED ))" -eq 0 ]; then
  echo "could not parse check lines; using binary exit code $rc" >&2
  [ "$rc" -eq 0 ] && { emit_ctrf "gfwx-roundtrip" 1 0 0; exit 0; }
  emit_ctrf "gfwx-roundtrip" 0 1 0; exit 1
fi

# A non-zero exit with zero parsed failures means the binary aborted (e.g. sanitizer) — count it.
if [ "$rc" -ne 0 ] && [ "$FAILED" -eq 0 ]; then
  FAILED=1
fi

emit_ctrf "gfwx-roundtrip" "$PASSED" "$FAILED" 0
