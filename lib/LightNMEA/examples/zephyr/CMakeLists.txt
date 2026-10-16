# ============================================================================
# LightNMEA Zephyr example - Zephyr application
# ============================================================================
# Приложение Zephyr, которое читает поток NMEA из UART с помощью API uart_pipe
# и парсит их библиотекой LightNMEA (NMEAStreamReader + Parser).
#
# Сборка (см. README.md):
#   west build -b native_sim -d build/native_sim
#   west build -b nrf52840dk/nrf52840 -d build/nrf52840dk
# ============================================================================

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gps_nmea)

target_sources(app PRIVATE src/main.cpp)

# Ядро LightNMEA (header-only, не зависит от Arduino/POSIX)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
