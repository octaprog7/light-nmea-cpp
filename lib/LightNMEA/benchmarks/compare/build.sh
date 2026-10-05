#!/bin/bash
#
# build.sh - сборка сравнительного бенчмарка NMEA-парсеров (bench_compare)
#
# Сравниваются:
#   - light-nmea-cpp (этот проект)
#   - minmea (kosma/minmea, чистый C)
#   - TinyGPSPlus (mikalhart/TinyGPSPlus)
#
# Использование:
#   ./build.sh                  собрать бинарник в build/bench_compare
#   ./build.sh run [файл.nmea]  собрать и запустить на данных
#   ./build.sh clean            удалить результаты сборки
#   ./build.sh help             показать справку
#
# Пример:
#   ./build.sh run ../../../../assets/nmea/trace_000.nmea
#
set -e

cd "$(dirname "$0")"

BUILD_DIR="build"
OUTPUT="$BUILD_DIR/bench_compare"
CXX="${CXX:-g++}"
CXX_STD="-std=c++17"
OPT="-O3"
INCLUDES="-I../../include -I. -Ithird_party -Ithird_party/TinyGPSPlus"
SOURCES="bench_compare.cpp third_party/minmea.c third_party/TinyGPSPlus/TinyGPS++.cpp"

show_help() {
    echo "Build and run the bench_compare comparative benchmark"
    echo ""
    echo "  ./build.sh                  build the binary"
    echo "  ./build.sh run [file.nmea]  build and run"
    echo "  ./build.sh clean            remove build/"
    echo "  ./build.sh help             show this help"
    echo ""
    echo "Example:"
    echo "  ./build.sh run ../../../../assets/nmea/trace_000.nmea"
}

do_build() {
    mkdir -p "$BUILD_DIR"
    echo "=== Building bench_compare ($CXX) ==="
    $CXX $CXX_STD $OPT $INCLUDES $SOURCES -o "$OUTPUT"
    echo "[OK] Binary: $OUTPUT"
}

do_run() {
    do_build
    local data="${1:-../../../../assets/nmea/trace_000.nmea}"
    if [ ! -f "$data" ]; then
        echo "Warning: '$data' not found - pass a path to an .nmea file."
        data=""
    fi
    "$OUTPUT" $data
}

case "${1:-}" in
    run)
        shift
        do_run "$@"
        ;;
    clean)
        rm -rf "$BUILD_DIR"
        echo "[OK] Removed $BUILD_DIR"
        ;;
    help|-h|--help)
        show_help
        ;;
    *)
        do_build
        ;;
esac