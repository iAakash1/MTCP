#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# benchmark.sh — MTCP throughput and latency benchmark runner
#
# Runs multiple rounds of the stress test with increasing client counts,
# collecting p50/p99 latency and throughput numbers for the README table.
#
# Usage:
#   ./tests/benchmark.sh                 # default: port 8080
#   ./tests/benchmark.sh 9000            # custom port
#   SERVER_THREADS=8 ./tests/benchmark.sh
#
# Prerequisites:
#   - Server must NOT be running (this script starts it)
#   - Python 3 in PATH
#   - Build the server first: make
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

PORT=${1:-8080}
THREADS=${SERVER_THREADS:-4}
SERVER_BIN="./server"

if [ ! -f "$SERVER_BIN" ]; then
    echo "❌ '$SERVER_BIN' not found. Run 'make' first."
    exit 1
fi

echo "══════════════════════════════════════════════════"
echo "  MTCP BENCHMARK"
echo "  Port: $PORT | Server threads: $THREADS"
echo "══════════════════════════════════════════════════"

# Raise file descriptor limit for high-concurrency runs
ulimit -n 65535 2>/dev/null || echo "⚠️  Could not raise ulimit (non-root). Results may be limited."

CLIENT_COUNTS=(50 100 200 500)

for COUNT in "${CLIENT_COUNTS[@]}"; do
    echo ""
    echo "── Round: $COUNT concurrent clients ──────────────"

    # Start fresh server
    $SERVER_BIN --port "$PORT" \
                --threads "$THREADS" \
                --backlog 512 \
                --queue-depth 2048 \
                --stats-interval 0 \
                --recv-timeout 5 \
                > /dev/null 2>&1 &
    SERVER_PID=$!
    sleep 0.3

    # Run stress test
    python3 tests/stress_test.py "$PORT" "$COUNT" || true

    # Stop server
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    sleep 0.2
done

echo ""
echo "══════════════════════════════════════════════════"
echo "  Benchmark complete."
echo "  Copy the latency numbers above into docs/benchmarks.md"
echo "══════════════════════════════════════════════════"
