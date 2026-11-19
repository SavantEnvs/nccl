#!/usr/bin/env bash
#
# mayhem/test.sh — RUN nccl's own unit tests that build without a GPU: the example tuner plugin's
# suite (plugins/tuner/example/test/test_plugin.c, built by mayhem/build.sh into build/test/).
# Every test asserts parsed config values / cost-table and channel outputs. exit 0 = pass. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
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

TEST_DIR="$SRC/build/test"
for r in test_plugin known_answer; do
  if [ ! -x "$TEST_DIR/$r" ]; then
    echo "test.sh: $TEST_DIR/$r missing — mayhem/build.sh should have built it" >&2
    emit_ctrf "nccl-unit" 0 1 0
    exit 1
  fi
done

# Upstream's tuner tests. Six cases (collective size topology channels regbuff pipeops) fail at
# upstream HEAD independent of this integration: they hand getCollInfo() an array of row pointers
# (float* cost_table_ptr[]) while plugin.c, like NCCL core (src/tuning/tuning.cc), reads the
# argument as one contiguous float[algo][proto] table. They are reported as skipped.
TUNER_CASES="init config-valid config-invalid fallback large-config stress-config empty-config nvl-domain constants"
TUNER_SKIPPED=6

WORK="$(mktemp -d "${TMPDIR:-/tmp}/nccl-test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
passed=0
failed=0

# run_suite <runner> <expected test count> <summary sed expr> [args...]: counts PASS:/FAIL: lines and
# cross-checks them against the runner's own summary line, so a runner that prints nothing fails.
run_suite() {
  local runner="$1" expect="$2" summary_re="$3"; shift 3
  local log="$WORK/$(basename "$runner").log" rc p f summary
  # The tuner suite writes its test_*.conf fixtures into the working directory.
  ( cd "$WORK" && "$runner" "$@" ) > "$log" 2>&1
  rc=$?
  cat "$log"
  p=$(grep -c '^PASS: ' "$log" || true)
  f=$(grep -c '^FAIL: ' "$log" || true)
  summary=$(sed -n "$summary_re" "$log")
  if [ "$((p + f))" -ne "$expect" ] || [ -z "$summary" ]; then
    echo "test.sh: $(basename "$runner") ran $((p + f)) checks (expected $expect), summary '${summary:-missing}'" >&2
    f=$(( expect - p > 0 ? expect - p : 1 ))
  elif [ "$rc" -ne 0 ] && [ "$f" -eq 0 ]; then
    f=1
  fi
  passed=$((passed + p))
  failed=$((failed + f))
}

# shellcheck disable=SC2086
run_suite "$TEST_DIR/test_plugin" 9 's/^Test Results: \([0-9]*\/[0-9]*\) tests passed$/\1/p' $TUNER_CASES
run_suite "$TEST_DIR/known_answer" 30 's/^Known-answer results: \([0-9]*\) failed$/\1/p'

emit_ctrf "nccl-unit" "$passed" "$failed" "$TUNER_SKIPPED"
