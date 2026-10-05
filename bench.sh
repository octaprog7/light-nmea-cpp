#!/bin/bash
#
# bench.sh - удобный запуск бенчмарков LightNMEA из корня репозитория.
#
# Делегирует вызов в lib/LightNMEA/benchmarks/run_benchmarks.sh.
#
# Сценарии:
#   ./bench.sh quick         LightNMEA только, быстрая проверка (100k, 0% ошибок)
#   ./bench.sh compare       все 4 реализации, 0% и 10% повреждённых пакетов
#   ./bench.sh types         сравнение реализаций по каждому типу сентенции
#   ./bench.sh navgen        бенчмарк генератора навигационных пакетов
#   ./bench.sh file [path]   сравнение на реальном .nmea-файле
#   ./bench.sh help          справка
#
# Настройка объёма: QUICK_COUNT=100 / COUNTS=200 ./bench.sh compare
#
set -euo pipefail

cd "$(dirname "$0")"

RUNNER="lib/LightNMEA/benchmarks/run_benchmarks.sh"

if [ ! -f "$RUNNER" ]; then
    echo "Error: $RUNNER not found" >&2
    exit 1
fi

exec "$RUNNER" "$@"