#!/usr/bin/env bash
# =============================================================================
# benchmark_latency.sh – Run high-throughput latency benchmark
#
# Starts server at maximum tick rate, runs client for 30 s, then
# prints latency CSV to ./results/latency_histogram.csv
# =============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/../build"
RESULTS="$SCRIPT_DIR/../results"
mkdir -p "$RESULTS"
PORT=9877 # use separate port to avoid conflict
NUM_SYMBOLS=100
TICK_RATE=500000 # 500K msg/s (max)
DURATION=30 # seconds
if [ ! -f "$BUILD/exchange_simulator" ] || [ ! -f "$BUILD/feed_handler" ]; then
 echo "[ERROR] Run: bash scripts/build.sh first"
 exit 1
fi
cleanup() {
 kill "$SIM_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
echo "============================================"
echo " Latency Benchmark"
echo "============================================"
echo " Tick rate : $TICK_RATE msg/s"
echo " Duration : ${DURATION}s"
echo " Symbols : $NUM_SYMBOLS"
echo ""
echo "[Bench] Starting server at $TICK_RATE msg/s..."
"$BUILD/exchange_simulator" "$PORT" "$NUM_SYMBOLS" "$TICK_RATE" &
SIM_PID=$!
sleep 1
echo "[Bench] Running client for ${DURATION}s..."
timeout "$DURATION" "$BUILD/feed_handler" "127.0.0.1" "$PORT" "$NUM_SYMBOLS" || true
echo ""
echo "[Bench] Complete. Check results/ for CSV output."
