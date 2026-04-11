#!/usr/bin/env bash
# =============================================================================
# run_client.sh – Start the Feed Handler (TCP client)
# Usage: bash run_client.sh [host] [port] [num_symbols]
# =============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/../build"
BIN="$BUILD/feed_handler"
HOST="${1:-127.0.0.1}"
PORT="${2:-9876}"
NUM_SYMBOLS="${3:-100}"
if [ ! -f "$BIN" ]; then
 echo "[ERROR] Binary not found: $BIN"
 echo " Run: bash scripts/build.sh"
 exit 1
fi
echo "============================================"
echo " Feed Handler (Client)"
echo "============================================"
echo " Host : $HOST"
echo " Port : $PORT"
echo " Symbols : $NUM_SYMBOLS"
echo " Binary : $BIN"
echo "--------------------------------------------"
echo " Press 'q' to quit, 'r' to reset stats"
echo ""
exec "$BIN" "$HOST" "$PORT" "$NUM_SYMBOLS"
