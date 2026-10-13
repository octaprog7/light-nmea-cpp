#!/usr/bin/env bash
#
# code_report.sh - создает отчёт о коде LightNMEA при запуске из корня репозитория.
#
# Реализация анализа - на Python (lib/LightNMEA/scripts/code_report.py);
# этот скрипт вызывает code_report.py.
#
# Варианты:
#   ./code_report.sh                авто: Cortex-M0+ (если есть arm-none-eabi-g++)
#   ./code_report.sh --native       сборка и анализ native (x86-64)
#   ./code_report.sh --m0           сборка и анализ Cortex-M0+ (soft-float)
#   ./code_report.sh --quick (-q)   вывод краткой информации
#   ./code_report.sh <файл>         анализ готового .o/.elf/.a
#   ./code_report.sh clean          удаляет build-code-report/
#   ./code_report.sh help           отображение справки
#
# Exit code: 0 - все проверки прошли успешно; 1 - найдены ошибки; 2 - ошибка вызова.
#
set -euo pipefail

cd "$(dirname "$0")"

PY="lib/LightNMEA/scripts/code_report.py"

if [ ! -f "$PY" ]; then
    echo "Error: $PY not found" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 not found (required by $PY)" >&2
    exit 1
fi

exec python3 "$PY" "$@"