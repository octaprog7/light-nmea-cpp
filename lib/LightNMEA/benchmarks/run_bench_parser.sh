#!/bin/bash
#
# run_bench_parser.sh - простой запуск бенчмарка NMEA-парсеров.
#
# Если бинарь ещё не собран - автоматически собирается через
# build_bench_parser.sh (gcc/g++ без CMake). Все аргументы передаются
# в bench_parser как есть (справка: ./run_bench_parser.sh -h).
#
# Примеры:
#   ./run_bench_parser.sh                     # показать справку
#   ./run_bench_parser.sh ALL 5 1000 RND ALL  # 1M ALL, 5% ошибок, все реализации
#   ./run_bench_parser.sh RMC 0 100           # 100k RMC без ошибок (LightNMEA)
#   ./run_bench_parser.sh GGA 10 50 LIBNAMEA  # 50k GGA, 10% ошибок, libnmea
#
set -euo pipefail

cd "$(dirname "$0")"

BIN="build-direct/bench_parser"

# Автосборка, если бинаря нет
if [ ! -x "$BIN" ]; then
    echo "Binary '$BIN' not found - building..."
    ./build_bench_parser.sh
fi

# Без аргументов - короткая справка о возможностях бенчмарка
if [ "$#" -eq 0 ]; then
    "$BIN" -h
    exit 0
fi

exec "$BIN" "$@"