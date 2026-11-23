#!/usr/bin/env bash

PASS_COUNT=0
FAIL_COUNT=0

SAMPLE_LOG="tests/sample_watchdog.log"
LIMIT_LOG="/tmp/wd_test_limit.log"
SIGINT_LOG="/tmp/wd_test_sigint.log"

rm -f "$SAMPLE_LOG" "$LIMIT_LOG" "$SIGINT_LOG"

check_pattern() {
    local label="$1"
    local pattern="$2"
    local file="$3"

    if grep -qE "$pattern" "$file"; then
        echo "[PASS] $label"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "[FAIL] $label (pattern not found:$pattern)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

check_absent() {
    local label="$1"
    local pattern="$2"
    local file="$3"

    if ! grep -qE "$pattern" "$file"; then
        echo "[PASS] $label"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "[FAIL] $label (unexpected pattern found:$pattern)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

echo "=== Scenario Group 1: Normal operation, normal exit, SIGUSR1, SIGKILL restart, SIGTERM shutdown ==="
./watchdog -l "$SAMPLE_LOG" -i 2 -t 3 \
    -w w-daemon1:daemon \
    -w w-normal:normal:2 \
    -w w-daemon2:daemon >/dev/null 2>&1 &
WD_PID=$!

sleep 1
kill -SIGUSR1 "$WD_PID"
sleep 2

W1_PID=$(grep "START.*worker=w-daemon1" "$SAMPLE_LOG" | tail -n 1 | sed -n 's/.*pid=\([0-9]*\).*/\1/p')
if [ -n "$W1_PID" ] && kill -0 "$W1_PID" 2>/dev/null; then
    kill -SIGKILL "$W1_PID"
    sleep 1
else
    echo "[FAIL] Could not verify w-daemon1 PID before SIGKILL"
    FAIL_COUNT=$((FAIL_COUNT + 1))
fi

kill -SIGTERM "$WD_PID"
wait "$WD_PID"

check_pattern "Normal operation starts multiple workers" "START.*worker=w-daemon2" "$SAMPLE_LOG"
check_pattern "SIGUSR1 status report written to log" "SIGUSR1 status report" "$SAMPLE_LOG"
check_pattern "Periodic check via SIGALRM/setitimer logged" "periodic check complete" "$SAMPLE_LOG"
check_pattern "Normal termination detected (exit code 0)" "worker=w-normal.*exited normally \(code=0\)" "$SAMPLE_LOG"
check_absent  "Normal termination does not trigger restart" "RESTART.*worker=w-normal" "$SAMPLE_LOG"
check_pattern "Unexpected SIGKILL termination detected" "worker=w-daemon1.*killed by signal 9" "$SAMPLE_LOG"
check_pattern "Unexpected termination triggers automatic restart" "RESTART.*worker=w-daemon1 attempt=1/3" "$SAMPLE_LOG"
check_pattern "Graceful shutdown on SIGTERM completes cleanly" "watchdog stopped cleanly" "$SAMPLE_LOG"

echo ""
echo "=== Scenario Group 2: Bounded restart limit (max 3) ==="
./watchdog -l "$LIMIT_LOG" -i 10 -t 2 -w w-fail:fail:1 >/dev/null 2>&1 &
WD_PID=$!
sleep 6
kill -SIGTERM "$WD_PID"
wait "$WD_PID"

check_pattern "Restart attempt 1/3 logged" "RESTART.*worker=w-fail attempt=1/3" "$LIMIT_LOG"
check_pattern "Restart attempt 2/3 logged" "RESTART.*worker=w-fail attempt=2/3" "$LIMIT_LOG"
check_pattern "Restart attempt 3/3 logged" "RESTART.*worker=w-fail attempt=3/3" "$LIMIT_LOG"
check_pattern "Restart limit enforced after 3 attempts" "LIMIT.*worker=w-fail reached max restarts \(3\)" "$LIMIT_LOG"
check_absent  "No 4th restart attempt made" "attempt=4" "$LIMIT_LOG"

echo ""
echo "=== Scenario Group 3: SIGINT (Ctrl+C) shutdown and SIGKILL timeout fallback ==="
./watchdog -l "$SIGINT_LOG" -i 10 -t 2 \
    -w w-clean:daemon \
    -w w-stubborn:stubborn >/dev/null 2>&1 &
WD_PID=$!
sleep 1
kill -SIGINT "$WD_PID"
wait "$WD_PID"

check_pattern "SIGINT (signal 2) triggers shutdown" "received signal 2, starting graceful shutdown" "$SIGINT_LOG"
check_pattern "Unresponsive worker terminated with SIGKILL after timeout" "KILL.*worker=w-stubborn.*sending SIGKILL" "$SIGINT_LOG"
check_pattern "All child processes reaped on SIGINT shutdown" "all child processes stopped and reaped" "$SIGINT_LOG"

rm -f "$LIMIT_LOG" "$SIGINT_LOG"

echo ""
echo "=== Summary: $PASS_COUNT passed,$FAIL_COUNT failed ==="
echo "Sample log saved to: $SAMPLE_LOG"

if [ "$FAIL_COUNT" -ne 0 ]; then
    exit 1
fi
exit 0