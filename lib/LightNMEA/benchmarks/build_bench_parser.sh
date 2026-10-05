#!/bin/bash
#
# build_bench_parser.sh - direct build of bench_parser with compiler and linker
# (no CMake): gcc/g++ + linking of object files.
#
# Builds:
#   - LightNMEA                    (header-only, just bench_parser.cpp)
#   - minmea                       (C, compare/third_party/minmea.c)
#   - TinyGPSPlus                  (C++, compare/third_party/TinyGPSPlus/)
#   - libnmea                      (C, static without dlopen; module symbols
#                                   renamed via -Dinit=... - equivalent of
#                                   'objcopy --redefine-sym' from upstream CMake)
#
# Result: build-direct/bench_parser (build-direct/ is in .gitignore).
#
# Usage:
#   ./build_bench_parser.sh        build the binary
#   ./build_bench_parser.sh clean  remove build-direct/
#
set -euo pipefail

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
cd "$(dirname "$0")"

CC="${CC:-gcc}"
CXX="${CXX:-g++}"
# Флаги оптимизации для честного замера скорости:
#   -O3           релизная оптимизация (измерение против конкурентов)
#   -march=native SIMD/FPU-инструкции текущей машины
#   -DNDEBUG      без assert в таймируемых циклах
# Переопределение при необходимости:  OPT="-O2" ./build_bench_parser.sh
OPT="${OPT:--O3 -march=native -DNDEBUG}"
STD="-std=c++17"
WARN="-Wall -Wextra"

# Пути инклудов
INC_LN="-I../include"                                    # light_nmea.hpp / nav_gen.hpp
INC_TP="-Icompare/third_party -Icompare/third_party/TinyGPSPlus -Icompare"
INC_LNMEA="-Icompare/third_party/libnmea/src/nmea -Icompare/third_party/libnmea/src/parsers"

OUT_DIR="build-direct"
BIN="$OUT_DIR/bench_parser"

case "${1:-}" in
    clean)
        rm -rf "$OUT_DIR"
        echo "[OK] Removed: $OUT_DIR"
        exit 0
        ;;
    help|-h|--help)
        sed -n '2,18p' "$SELF" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
esac

mkdir -p "$OUT_DIR"

echo "=== Building bench_parser ($CXX / $CC) ==="
echo "  Optimization flags: $OPT"

# ---- 1) C-часть: minmea ----
$CC $OPT -w -c compare/third_party/minmea.c \
    -o "$OUT_DIR/minmea.o" $INC_TP

# ---- 2) C-часть: libnmea (ядро + статическая регистрация парсеров) ----
$CC $OPT -w -c compare/third_party/libnmea/src/nmea/nmea.c \
    -o "$OUT_DIR/libnmea_nmea.o" $INC_LNMEA
$CC $OPT -w \
    -DENABLE_GPGGA -DENABLE_GPGLL -DENABLE_GPRMC -DENABLE_GPVTG -DPARSER_COUNT=4 \
    -c compare/third_party/libnmea/src/nmea/parser_static.c \
    -o "$OUT_DIR/libnmea_parser_static.o" $INC_LNMEA
$CC $OPT -w -c compare/third_party/libnmea/src/parsers/parse.c \
    -o "$OUT_DIR/libnmea_parse_helpers.o" $INC_LNMEA

# Модули парсеров: переименование конфликтующих символов init/parse/... на
# препроцессоре, чтобы четыре модуля ужились в одном бинарнике.
for mod in gpgga gpgll gprmc gpvtg; do
    $CC $OPT -w \
        -Dinit=nmea_${mod}_init \
        -Dparse=nmea_${mod}_parse \
        -Dset_default=nmea_${mod}_set_default \
        -Dallocate_data=nmea_${mod}_allocate_data \
        -Dfree_data=nmea_${mod}_free_data \
        -c compare/third_party/libnmea/src/parsers/${mod}.c \
        -o "$OUT_DIR/libnmea_${mod}.o" $INC_LNMEA
done

# ---- 3) C++-часть: TinyGPSPlus и сам бенчмарк ----
$CXX $OPT $STD $WARN -c compare/third_party/TinyGPSPlus/TinyGPS++.cpp \
    -o "$OUT_DIR/TinyGPS++.o" $INC_TP
# Важно: "..." вокруг \"$OPT\" - чтобы макрос BENCH_OPT_STR получил ВСЁ значение
# с пробелами (-O3 -march=native -DNDEBUG) как одну строку; без кавычек шелл
# разбил бы $OPT на слова и макрос стал бы битым ("-O3).
$CXX $OPT $STD $WARN -DBENCH_OPT_STR="\"$OPT\"" -c bench_parser.cpp \
    -o "$OUT_DIR/bench_parser.o" $INC_LN $INC_TP $INC_LNMEA

# ---- 4) Линковка ----
$CXX $OPT $STD -o "$BIN" \
    "$OUT_DIR/bench_parser.o" \
    "$OUT_DIR/minmea.o" \
    "$OUT_DIR/TinyGPS++.o" \
    "$OUT_DIR/libnmea_nmea.o" \
    "$OUT_DIR/libnmea_parser_static.o" \
    "$OUT_DIR/libnmea_parse_helpers.o" \
    "$OUT_DIR/libnmea_gpgga.o" \
    "$OUT_DIR/libnmea_gpgll.o" \
    "$OUT_DIR/libnmea_gprmc.o" \
    "$OUT_DIR/libnmea_gpvtg.o"

echo ""
echo "[OK] Binary built: $BIN"
echo "     Run: ./run_bench_parser.sh [arguments]"