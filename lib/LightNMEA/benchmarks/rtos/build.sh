#!/usr/bin/env bash
# ============================================================================
# LightNMEA RTOS benchmarks - build script
# ============================================================================
# Собирает FreeRTOS-бенчмарк (POSIX-порт, хост), Zephyr-бенчмарк
# (nrf52840dk) и ESP32-бенчмарк (D1 mini). Для Zephyr нужны west + Zephyr SDK,
# для ESP32 - PlatformIO (см. README.md).
#
# Использование:
#   ./build.sh            # собрать всё
#   ./build.sh freertos   # только FreeRTOS
#   ./build.sh zephyr     # только Zephyr (nrf52840dk)
#   ./build.sh esp32      # только ESP32 (D1 mini)
# ============================================================================

set -euo pipefail
cd "$(dirname "$0")"

# west может лежать вне PATH (например, в penv PlatformIO)
find_west() {
  if command -v west >/dev/null 2>&1; then
    command -v west
  elif [ -x "$HOME/.platformio/penv/bin/west" ]; then
    echo "$HOME/.platformio/penv/bin/west"
  else
    echo ""
  fi
}

build_freertos() {
  echo "=== FreeRTOS (POSIX port) ==="
  cmake -S freertos -B freertos/build -DCMAKE_BUILD_TYPE=Release
  cmake --build freertos/build -j
  echo "OK: freertos/build/freertos_nmea_bench"
}

build_zephyr() {
  echo "=== Zephyr (nrf52840dk) ==="
  local west_bin
  west_bin="$(find_west)"
  if [ -z "$west_bin" ]; then
    echo "west not found; set up Zephyr environment (see README.md)" >&2
    exit 1
  fi
  if [ -z "${ZEPHYR_BASE:-}" ]; then
    echo "ZEPHYR_BASE is not set; export it before running (see README.md)" >&2
    exit 1
  fi
  "$west_bin" build -b nrf52840dk/nrf52840 -d zephyr/build/nrf52840dk
  echo "OK: zephyr/build/nrf52840dk/zephyr/zephyr.elf"
}

build_esp32() {
  echo "=== ESP32 (D1 mini) ==="
  local pio_bin
  pio_bin="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
  if [ ! -x "$pio_bin" ]; then
    echo "PlatformIO not found; install it (see README.md)" >&2
    exit 1
  fi
  "$pio_bin" run -e d1_mini32 -d esp32
  echo "OK: esp32/.pio/build/d1_mini32/firmware.bin"
}

case "${1:-all}" in
  freertos) build_freertos ;;
  zephyr)   build_zephyr ;;
  esp32)    build_esp32 ;;
  all)      build_freertos; build_zephyr; build_esp32 ;;
  *) echo "Usage: $0 [freertos|zephyr|esp32|all]" >&2; exit 1 ;;
esac