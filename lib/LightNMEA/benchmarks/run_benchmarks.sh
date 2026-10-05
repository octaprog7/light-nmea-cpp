#!/bin/bash
#
# run_benchmarks.sh - typical benchmark scenarios for LightNMEA.
#
# Scenarios:
#   ./run_benchmarks.sh               show this help
#   ./run_benchmarks.sh quick         LightNMEA only, quick check
#                                     (ALL, 0% errors, 100k packets)
#   ./run_benchmarks.sh compare       all 4 implementations (LIGHT/MINMEA/TINYGPS/LIBNAMEA)
#                                     on a mix of RMC/GGA/VTG/GLL:
#                                       0% corruption and 10% corrupted packets
#   ./run_benchmarks.sh types         compare implementations per sentence type
#                                     separately (RMC, GGA, VTG, GLL)
#   ./run_benchmarks.sh navgen        navigation packet generator benchmark
#                                     (bench_nav_gen, no arguments)
#   ./run_benchmarks.sh file [path]   bench_compare on a real .nmea file
#                                     (LightNMEA vs minmea vs TinyGPSPlus)
#
# Environment variables to tune the volume (thousands of packets):
#   QUICK_COUNT=100   ./run_benchmarks.sh quick
#   COUNTS=200        ./run_benchmarks.sh compare
#
# If binaries are not built, they are built automatically:
#   bench_parser   -> build_bench_parser.sh (gcc/g++ direct build)
#   bench_nav_gen  -> direct g++ build (header-only, no dependencies)
#
# Exit code: 0 - all scenarios succeeded; 1 - error.
#
set -euo pipefail

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
cd "$(dirname "$0")"

BIN="build-direct/bench_parser"
NAVBIN="build-direct/bench_nav_gen"

QUICK_COUNT="${QUICK_COUNT:-100}"     # тыс. пакетов в quick
COUNTS="${COUNTS:-200}"               # тыс. пакетов в compare/types

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

step()  { echo -e "${GREEN}=== $* ===${NC}"; }
info()  { echo -e "${YELLOW}>>> $*${NC}"; }

# ---------------------------------------------------------------------------
# Автосборка бинарей при необходимости
# ---------------------------------------------------------------------------
ensure_bench_parser() {
    if [ ! -x "$BIN" ]; then
        info "Binary '$BIN' not found - building (build_bench_parser.sh)..."
        ./build_bench_parser.sh
    fi
}

ensure_bench_nav_gen() {
    if [ ! -x "$NAVBIN" ]; then
        info "Binary '$NAVBIN' not found - building directly (g++)..."
        mkdir -p build-direct
        g++ -std=c++17 -O3 -march=native -DNDEBUG \
            -I../include bench_nav_gen.cpp -o "$NAVBIN"
    fi
}

# ---------------------------------------------------------------------------
# Сценарии
# ---------------------------------------------------------------------------

scenario_quick() {
    ensure_bench_parser
    step "Quick: LightNMEA, ALL, 0% errors, ${QUICK_COUNT}k pkt"
    "$BIN" ALL 0 "$QUICK_COUNT" RND LIGHT
}

scenario_compare() {
    ensure_bench_parser
    step "Compare (all impls), ALL, 0% errors, ${COUNTS}k pkt"
    "$BIN" ALL 0 "$COUNTS" RND ALL
    step "Compare (all impls), ALL, 10% corrupted, ${COUNTS}k pkt"
    "$BIN" ALL 10 "$COUNTS" RND ALL
}

scenario_types() {
    ensure_bench_parser
    for t in RMC GGA VTG GLL; do
        step "Compare (all impls), ${t}, 0% errors, ${COUNTS}k pkt"
        "$BIN" "$t" 0 "$COUNTS" RND ALL
    done
}

scenario_navgen() {
    ensure_bench_nav_gen
    step "nav_gen benchmark (10M iterations)"
    "$NAVBIN"
}

scenario_file() {
    local data="${1:-}"
    if [ -z "$data" ]; then
        echo "Usage: $0 file [path_to_file.nmea]"
        return 1
    fi
    step "Real-file benchmark: $(basename "$data")"
    ./compare/build.sh run "$data"
}

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
help() {
    sed -n '2,24p' "$SELF" | sed 's/^# \{0,1\}//'
}

case "${1:-}" in
    quick)   scenario_quick ;;
    compare) scenario_compare ;;
    types)   scenario_types ;;
    navgen)  scenario_navgen ;;
    file)    scenario_file "${2:-}" ;;
    help|-h|--help) help ;;
    *)
        help
        exit 1
        ;;
esac