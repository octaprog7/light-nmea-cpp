# ============================================================================
# LightNMEA Zephyr benchmark - Zephyr application
# ============================================================================
# Сравнительный бенчмарк NMEA-парсеров (LightNMEA vs minmea vs TinyGPSPlus)
# в главном потоке Zephyr.
#
# Сборка (см. README.md):
#   west build -b native_sim -d build/native_sim
#   west build -b nrf52840dk/nrf52840 -d build/nrf52840dk
# ============================================================================

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gps_nmea_bench)

# Vendored копии конкурентов из benchmarks/firmware/lib (синхронны с
# benchmarks/compare/third_party)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/lib)

target_sources(app PRIVATE
  src/main.cpp
  ${TP}/minmea/src/minmea.c
  ${TP}/TinyGPSPlus/src/TinyGPS++.cpp
)

# Ядро LightNMEA (header-only, не зависит от Arduino/POSIX) + конкуренты
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
  ${TP}/minmea/include
  ${TP}/TinyGPSPlus/include
  ${CMAKE_CURRENT_SOURCE_DIR}/..   # Arduino.h shim
)

# TinyGPS++.cpp не должен определять свою chrono-версию millis()
# (иначе - конфликт символов с RTOS-версией бенча).
set_source_files_properties(${TP}/TinyGPSPlus/src/TinyGPS++.cpp PROPERTIES
    COMPILE_DEFINITIONS "ARDUINO=1")

# picolibc не имеет timegm(); mktime эквивалентен (TZ на плате = UTC),
# как в firmware-бенчмарке (platformio.ini: -Dtimegm=mktime).
set_source_files_properties(${TP}/minmea/src/minmea.c PROPERTIES
    COMPILE_DEFINITIONS "timegm=mktime")

# nRF52840: системный таймер - RTC 32.768 кГц, поэтому
# CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC даёт mhz=0 и метрики cycles/pkt
# обнуляются. Поэтому для CPU-циклов задаю частоту ядра явно (как в firmware-бенчмарке).
if(CONFIG_SOC_NRF52840)
  target_compile_definitions(app PRIVATE F_CPU=64000000)
endif()