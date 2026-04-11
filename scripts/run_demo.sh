#!/usr/bin/env bash
# =============================================================================
# run_demo.sh – Launch server + client together (auto cleanup on exit)
# =============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/../build"
PORT=9876
NUM_SYMBOLS=100
TICK_RATE=50000
if [ ! -f "$BUILD/exchange_simulator" ] || [ ! -f "$BUILD/feed_handler" ]; then
 echo "[ERROR] Binaries missing. Run: bash scripts/build.sh"
 exit 1
fi
cleanup() {
 echo ""
 echo "[Demo] Shutting down..."
 kill "$SIM_PID" 2>/dev/null || true
 wait "$SIM_PID" 2>/dev/null || true
 echo "[Demo] Done."
}
trap cleanup EXIT INT TERM
echo "============================================"
echo " NSE Market Data Feed Handler – Full Demo"
echo "============================================"
echo ""
echo "[Demo] Starting Exchange Simulator on port $PORT ..."
"$BUILD/exchange_simulator" "$PORT" "$NUM_SYMBOLS" "$TICK_RATE" &
SIM_PID=$!
sleep 1
echo "[Demo] Starting Feed Handler (press 'q' to quit)..."
"$BUILD/feed_handler" "127.0.0.1" "$PORT" "$NUM_SYMBOLS"
t