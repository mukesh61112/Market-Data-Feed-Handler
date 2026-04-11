#!/usr/bin/env bash
# =============================================================================
# build.sh – Build the Market Data Feed Handler project
# =============================================================================
set -uo pipefail # NOTE: no -e so we can handle errors ourselves
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT/build"
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
info() { echo -e "${CYAN}[INFO]${RESET} $*"; }
ok() { echo -e "${GREEN}[OK]${RESET} $*"; }
warn() { echo -e "${YELLOW}[WARN]${RESET} $*"; }
die() { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }
echo -e "${BOLD}============================================${RESET}"
echo -e "${BOLD} NSE Market Data Feed Handler – Build ${RESET}"
echo -e "${BOLD}============================================${RESET}"
echo ""
info "Project root : $ROOT"
info "Build dir : $BUILD_DIR"
echo ""
# ■■ Check dependencies ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
info "Checking dependencies..."
command -v cmake &>/dev/null || die "cmake not found. Install: sudo apt install cmake"
command -v g++ &>/dev/null || die "g++ not found. Install: sudo apt install g++"
command -v make &>/dev/null || die "make not found. Install: sudo apt install make"
GCC_VER=$(g++ -dumpversion | cut -d. -f1)
(( GCC_VER >= 9 )) || die "g++ version $GCC_VER is too old. Need >= 9 for C++17."
ok "g++ $GCC_VER | cmake $(cmake --version | head -1 | awk '{print $3}') | make $(make --version | head -1 |
 awk '{print $3}')"
# ■■ Optional clean ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
if [[ "${1:-}" == "--clean" ]]; then
 warn "Cleaning $BUILD_DIR ..."
 rm -rf "$BUILD_DIR"
fi
# ■■ CMake configure ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
info "Configuring CMake (Release) ..."
mkdir -p "$BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" \
 -DCMAKE_BUILD_TYPE=Release \
 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
CMAKE_RC=$?
if [ $CMAKE_RC -ne 0 ]; then
 die "CMake configuration failed (exit $CMAKE_RC). Check output above."
fi
ok "CMake configuration done"
# ■■ Compile ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
NPROC=$(nproc 2>/dev/null || echo 4)
info "Compiling with $NPROC parallel jobs ..."
cmake --build "$BUILD_DIR" --parallel "$NPROC"
BUILD_RC=$?
if [ $BUILD_RC -ne 0 ]; then
 die "Compilation failed (exit $BUILD_RC). Check errors above."
fi
# ■■ Done ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
echo ""
ok "Build complete!"
echo ""
echo -e "${BOLD}Binaries:${RESET}"
ls -lh "$BUILD_DIR/exchange_simulator" "$BUILD_DIR/feed_handler"
echo ""
echo -e "${BOLD}Quick start:${RESET}"
echo " bash scripts/run_server.sh # start exchange simulator"
echo " bash scripts/run_client.sh # start feed handler"
echo " bash scripts/run_demo.sh # start both together"
echo " bash scripts/benchmark_latency.sh # 500K msg/s stress test"
echo ""
