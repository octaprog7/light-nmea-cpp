#!/usr/bin/env bash
#
# build_fw.sh - универсальная сборка прошивки LightNMEA GPS Logger (src/main.cpp)
#
# Два режима:
#
# 1) Нативная сборка на хосте (x86) — проверка логики без железа.
#    src/main.cpp компилируется вместе с Arduino-шимом (native/), где:
#      - Serial  (отладочный вывод) -> stdout
#      - Serial1 (GPS-вход)         -> stdin
#    Поэтому NMEA можно подавать из файла или конвейером:
#      cat track.nmea | ./build_fw.sh run --csv-only
#
# 2) Прошивка под плату — делегирование в build_pico.sh (PlatformIO, env:pico).
#
# Сценарии:
#   ./build_fw.sh                   сборка нативного бинарника (alias: native, build)
#   ./build_fw.sh run [file] [--csv-only]
#                                   собрать и запустить; file - .nmea-файл
#                                   (без file читает stdin); --csv-only - только CSV-строки
#   ./build_fw.sh pico              сборка прошивки Pico (PlatformIO)
#   ./build_fw.sh upload            сборка + заливка прошивки на Pico
#   ./build_fw.sh monitor           последовательный монитор
#   ./build_fw.sh clean             очистка нативного билда + кэша PlatformIO
#   ./build_fw.sh help              справка
#
# Переменные окружения: CXX (компилятор, default g++), CXXFLAGS (флаги).
#
set -euo pipefail

cd "$(dirname "$0")"

# ============================================================================
# Конфигурация нативной сборки
# ============================================================================
BUILD_DIR="build-native"
SRC="src/main.cpp"
SHIM_SRC="native/arduino_native.cpp"
SHIM_INC="native"
LIB_INC="lib/LightNMEA/include"
BIN="$BUILD_DIR/lightnmea_logger"

CXX="${CXX:-g++}"
# ARDUINO_ARCH_SAMD -> нейтральная ветка src/main.cpp (Serial1::begin(baud, cfg, rx, tx)),
# которая в шиме сводится к no-op; реальный источник данных - stdin.
CXXFLAGS="${CXXFLAGS:--std=c++17 -O2 -Wall -Wextra -DARDUINO_ARCH_SAMD}"

PICO_SCRIPT="./build_pico.sh"

# Канонический CSV-поток платы начинается с поля valid (0/1):
#   valid,satellites,latitude,longitude,speed,course,altitude,time,date,constellation,fix_mode,hdop
CSV_RE='^[01],'

# Цвета (если stdout - терминал)
if [ -t 1 ]; then
    RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'
    BLUE=$'\033[0;34m'; NC=$'\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BLUE=''; NC=''
fi

die() { echo -e "${RED}Error: $*${NC}" >&2; exit 1; }

show_help() {
    cat <<'EOF'
=== LightNMEA GPS Logger - Build Script ===

Usage:
  ./build_fw.sh                    Build native (x86) binary
  ./build_fw.sh run [file] [--csv-only]
                                   Build and run; NMEA from file or stdin
  ./build_fw.sh pico               Build Pico firmware (PlatformIO)
  ./build_fw.sh upload             Build + upload firmware to Pico
  ./build_fw.sh monitor            Open serial monitor
  ./build_fw.sh clean              Clean native build + PlatformIO cache
  ./build_fw.sh help               Show this help

Native mode maps Serial -> stdout, Serial1 -> stdin.
Examples:
  printf '$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\n' \\
      | ./build_fw.sh run --csv-only
  ./build_fw.sh run track.nmea --csv-only > out.csv
EOF
}

# ============================================================================
# Нативная сборка
# ============================================================================
do_build() {
    echo -e "${BLUE}=== Building native (host) binary ===${NC}"
    mkdir -p "$BUILD_DIR"
    # shellcheck disable=SC2086
    "$CXX" $CXXFLAGS -I"$SHIM_INC" -I"$LIB_INC" "$SRC" "$SHIM_SRC" -o "$BIN"
    echo -e "${GREEN}[OK] Binary: $BIN${NC}"
}

# ============================================================================
# Запуск нативного бинарника (NMEA из файла или stdin)
# ============================================================================
do_run() {
    local file="" csv_only=0

    while [ $# -gt 0 ]; do
        case "$1" in
            --csv-only) csv_only=1 ;;
            -h|--help) show_help; exit 0 ;;
            *)
                if [ -n "$file" ]; then
                    die "unexpected argument: $1 (use './build_fw.sh help')"
                fi
                file="$1"
                ;;
        esac
        shift
    done

    # В режиме --csv-only сообщения сборки уходят в stderr, чтобы stdout
    # оставался чистым (удобно для перенаправления: ... > out.csv).
    if [ "$csv_only" -eq 1 ]; then
        do_build 1>&2
    else
        do_build
    fi

    if [ -n "$file" ]; then
        [ -f "$file" ] || die "NMEA file not found: $file"
        if [ "$csv_only" -eq 1 ]; then
            "$BIN" < "$file" | grep -E "$CSV_RE" || true
        else
            "$BIN" < "$file"
        fi
    else
        if [ "$csv_only" -eq 1 ]; then
            "$BIN" | grep -E "$CSV_RE" || true
        else
            "$BIN"
        fi
    fi
}

# ============================================================================
# Делегирование в build_pico.sh (прошивка)
# ============================================================================
do_pico() {
    [ -f "$PICO_SCRIPT" ] || die "$PICO_SCRIPT not found"
    exec "$PICO_SCRIPT" "$@"
}

# ============================================================================
# Очистка
# ============================================================================
do_clean() {
    echo -e "${YELLOW}=== Cleaning ===${NC}"
    rm -rf "$BUILD_DIR"
    echo -e "${GREEN}[OK] Removed $BUILD_DIR${NC}"
    if [ -f "$PICO_SCRIPT" ]; then
        "$PICO_SCRIPT" clean
    fi
}

# ============================================================================
# Аргументы
# ============================================================================
case "${1:-}" in
    ""|native|build)
        do_build
        ;;
    run)
        shift
        do_run "$@"
        ;;
    pico)
        do_pico
        ;;
    upload)
        do_pico upload
        ;;
    monitor)
        do_pico monitor
        ;;
    clean)
        do_clean
        ;;
    help|-h|--help)
        show_help
        ;;
    *)
        die "unknown command: $1 (use './build_fw.sh help')"
        ;;
esac