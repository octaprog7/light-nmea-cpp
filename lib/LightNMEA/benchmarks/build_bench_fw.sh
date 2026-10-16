#!/usr/bin/env bash
#
# build_bench_fw.sh - сборка и запуск on-target сравнительного бенчмарка
# (bench_compare_mcu) для Raspberry Pi Pico (RP2040) и ESP32.
#
# Прошивка сравнивает на плате LightNMEA vs minmea vs TinyGPSPlus на
# одинаковых пакетах (nav_gen.hpp) и выводит pkt/s по Serial @ 115200.
#
# Использование:
#   ./build_bench_fw.sh                    собрать все env (pico, esp32, d1_mini32,
#                                          esp32s3, esp32c3, esp8266, bluepill,
#                                          teensy41, feather_m0)
#   ./build_bench_fw.sh pico               собрать только pico
#   ./build_bench_fw.sh esp32              собрать только esp32
#   ./build_bench_fw.sh d1_mini32          WEMOS D1 mini ESP32
#   ./build_bench_fw.sh esp32s3            ESP32-S3-DevKitC-1
#   ./build_bench_fw.sh esp32c3            ESP32-C3-DevKitM-1
#   ./build_bench_fw.sh esp8266            WEMOS/NodeMCU D1 mini ESP8266
#   ./build_bench_fw.sh bluepill           STM32 Blue Pill (F103C8)
#   ./build_bench_fw.sh teensy41           Teensy 4.1
#   ./build_bench_fw.sh feather_m0         Adafruit Feather M0 (SAMD21)
#   ./build_bench_fw.sh pico upload        собрать и залить на плату
#   ./build_bench_fw.sh esp32 monitor      открыть последовательный монитор
#   ./build_bench_fw.sh clean              очистить результаты сборки
#   ./build_bench_fw.sh help               справка
#
# Требуется PlatformIO (pio в PATH или $HOME/.platformio/penv/bin/pio).
# Объём прогона: BENCH_PACKET_COUNT=50000 ./build_bench_fw.sh pico
#
set -euo pipefail

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
cd "$(dirname "$0")"

FW_DIR="firmware"

# Локализация PlatformIO CLI
if command -v pio >/dev/null 2>&1; then
    PIO="pio"
elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
    PIO="$HOME/.platformio/penv/bin/pio"
else
    echo "Error: PlatformIO not found (install it or add 'pio' to PATH)" >&2
    exit 1
fi

if [ ! -d "$FW_DIR" ]; then
    echo "Error: $FW_DIR not found (run from lib/LightNMEA/benchmarks/)" >&2
    exit 1
fi

ENV="${1:-all}"
ACTION="${2:-run}"

# Вспомогательный вывод справки
show_help() {
    awk 'NR > 1 { if ($0 ~ /^#/) { sub(/^# /, ""); print } else exit }' "$SELF"
}

run_pio() {
    (cd "$FW_DIR" && "$PIO" "$@")
}

case "$ENV" in
    help|-h|--help)
        show_help
        exit 0
        ;;
    pico|esp32|d1_mini32|esp32s3|esp32c3|esp8266|bluepill|teensy41|feather_m0|all) ;;
    *) echo "Error: unknown env '$ENV' (use pico | esp32 | d1_mini32 | esp32s3 | esp32c3 | esp8266 | bluepill | teensy41 | feather_m0 | all)" >&2; exit 1 ;;
esac

case "$ACTION" in
    run|upload|monitor|clean) ;;
    *) echo "Error: unknown action '$ACTION' (use run | upload | monitor | clean)" >&2; exit 1 ;;
esac

case "$ACTION" in
    run)
        echo "=== Building benchmark firmware ($ENV) ==="
        if [ "$ENV" = "all" ]; then
            run_pio run
        else
            run_pio run -e "$ENV"
        fi
        ;;
    upload)
        [ "$ENV" = "all" ] && ENV="pico"
        echo "=== Building + uploading benchmark firmware ($ENV) ==="
        run_pio run -e "$ENV" -t upload
        ;;
    monitor)
        echo "=== Serial monitor (reboot the board to re-run) ==="
        run_pio device monitor
        ;;
    clean)
        echo "=== Cleaning benchmark build ==="
        if [ "$ENV" = "all" ]; then
            run_pio run -t clean
        else
            run_pio run -e "$ENV" -t clean
        fi
        ;;
esac