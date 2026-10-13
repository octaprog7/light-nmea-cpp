#!/bin/bash
#
# bench.sh - удобный запуск бенчмарков LightNMEA из корня репозитория.
#
# Вызывает lib/LightNMEA/benchmarks/run_benchmarks.sh.
#
# Варианты:
#   ./bench.sh quick         быстрая проверка LightNMEA, 100k сентенций, 0% ошибок
#   ./bench.sh compare       все 4 реализации, 0% и 10% повреждённых пакетов/сентенций
#   ./bench.sh types         сравнение реализаций по каждому типу сентенции/пакета
#   ./bench.sh navgen        бенч генератора навигационных пакетов/сентенций
#   ./bench.sh file [path]   сравнение на реальном .nmea-файле
#   ./bench.sh help          обображение справки
#
# Настройка кол-ва сентенций: QUICK_COUNT=100 / COUNTS=200 ./bench.sh compare
#
set -euo pipefail

cd "$(dirname "$0")"

RUNNER="lib/LightNMEA/benchmarks/run_benchmarks.sh"

if [ ! -f "$RUNNER" ]; then
    echo "Error: $RUNNER not found" >&2
    exit 1
fi

exec "$RUNNER" "$@"