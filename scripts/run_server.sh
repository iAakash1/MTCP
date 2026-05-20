#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# run_server.sh — production-like server startup script
#
# Sets OS-level limits and starts the server with recommended production flags.
# Run this instead of ./server directly when doing load testing.
#
# Usage:
#   ./scripts/run_server.sh
#   PORT=9000 THREADS=16 ./scripts/run_server.sh
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

PORT=${PORT:-8080}
THREADS=${THREADS:-8}
BACKLOG=${BACKLOG:-256}
QUEUE_DEPTH=${QUEUE_DEPTH:-2048}
RECV_TIMEOUT=${RECV_TIMEOUT:-10}
STATS_INTERVAL=${STATS_INTERVAL:-10}

SERVER_BIN="./server"
if [ ! -f "$SERVER_BIN" ]; then
    echo "❌ '$SERVER_BIN' not found. Run 'make' first."
    exit 1
fi

# ── Raise file descriptor limit ────────────────────────────────────────────────
# Each client connection uses one file descriptor.
# Default limit (1024) is exhausted by ~512 concurrent clients (stdin/stdout/stderr = 3 reserved).
# Set to 65535 to support tens of thousands of concurrent connections.
echo "[startup] Setting ulimit -n 65535..."
ulimit -n 65535 2>/dev/null || {
    echo "⚠️  Warning: could not raise fd limit (try: sudo ulimit -n 65535)"
    echo "   Current limit: $(ulimit -n)"
}

echo "[startup] fd limit: $(ulimit -n)"
echo "[startup] Starting MTCP server..."
echo ""

exec "$SERVER_BIN" \
    --port           "$PORT"           \
    --threads        "$THREADS"        \
    --backlog        "$BACKLOG"        \
    --queue-depth    "$QUEUE_DEPTH"    \
    --recv-timeout   "$RECV_TIMEOUT"   \
    --stats-interval "$STATS_INTERVAL"
