 #!/usr/bin/env bash
# =============================================================================
# run_server.sh – Start the Exchange Simulator (TCP server)
# Usage: bash run_server.sh [port] [num_symbols] [tick_rate]
# =============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/../build"
BIN="$BUILD/exchange_simulator"
PORT="${1:-9876}"
NUM_SYMBOLS="${2:-100}"
TICK_RATE="${3:-50000}"
if [ ! -f "$BIN" ]; then
 echo "[ERROR] Binary not found: $BIN"
 echo " Run: bash scripts/build.sh"
 exit 1
fi
echo "============================================"
echo " Exchange Simulator (Server)"
echo "============================================"
echo " Port : $PORT"
echo " Symbols : $NUM_SYMBOLS"
echo " Tick rate : $TICK_RATE msg/s"
echo " Binary : $BIN"
echo "--------------------------------------------"
echo " Press Ctrl+C to stop"
echo ""
exec "$BIN" "$PORT" "$NUM_SYMBOLS" "$TICK_RATE"
