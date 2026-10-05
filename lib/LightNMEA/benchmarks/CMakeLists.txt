cmake_minimum_required(VERSION 3.14)
project(LightNMEA_Benchmarks C CXX)

# C++17 стандарт
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Оптимизация Release
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_BUILD_TYPE Release)

# Пути к заголовочным файлам
include_directories(${CMAKE_SOURCE_DIR}/../include)

# ============================================================================
# Конкуренты для сравнения (benchmarks/compare/third_party)
# ============================================================================
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/compare/third_party)

# --- minmea (чистый C, компилируется как C) ---
add_library(minmea STATIC ${TP}/minmea.c)
target_include_directories(minmea PUBLIC ${TP})

# --- TinyGPSPlus (C++, нужен shim Arduino.h из benchmarks/compare/) ---
add_library(tinygpsplus STATIC ${TP}/TinyGPSPlus/TinyGPS++.cpp)
target_include_directories(tinygpsplus PUBLIC
    ${TP}/TinyGPSPlus
    ${CMAKE_CURRENT_SOURCE_DIR}/compare)  # Arduino.h host-shim

# --- libnmea (jacketizer): статическая сборка БЕЗ dlopen ---
# Вместо 'objcopy --redefine-sym' (как в оригинальном CMake) переименование
# символов init/parse/... выполняется на препроцессоре (-Dinit=nmea_gpgga_init ...),
# что даёт тот же результат: модули не конфликтуют друг с другом, а
# parser_static.c регистрирует их по именам nmea_<mod>_*.
# Подключаем только нужные бенчмарку типы: GGA, GLL, RMC, VTG.
set(LIBNMEA_SRC
    ${TP}/libnmea/src/nmea/nmea.c
    ${TP}/libnmea/src/nmea/parser_static.c
    ${TP}/libnmea/src/parsers/parse.c
    ${TP}/libnmea/src/parsers/gpgga.c
    ${TP}/libnmea/src/parsers/gpgll.c
    ${TP}/libnmea/src/parsers/gprmc.c
    ${TP}/libnmea/src/parsers/gpvtg.c)

foreach(mod gpgga gpgll gprmc gpvtg)
    set(_nmea_defs
        "init=nmea_${mod}_init"
        "parse=nmea_${mod}_parse"
        "set_default=nmea_${mod}_set_default"
        "allocate_data=nmea_${mod}_allocate_data"
        "free_data=nmea_${mod}_free_data")
    set_source_files_properties(${TP}/libnmea/src/parsers/${mod}.c PROPERTIES
        COMPILE_DEFINITIONS "${_nmea_defs}")
endforeach()

add_library(libnmea STATIC ${LIBNMEA_SRC})
target_include_directories(libnmea PUBLIC
    ${TP}/libnmea/src/nmea
    ${TP}/libnmea/src/parsers)
target_compile_definitions(libnmea PRIVATE
    ENABLE_GPGGA ENABLE_GPGLL ENABLE_GPRMC ENABLE_GPVTG
    PARSER_COUNT=4)

# ============================================================================
# Бенчмарки
# ============================================================================
add_executable(bench_parser bench_parser.cpp)
target_compile_options(bench_parser PRIVATE -Wall -Wextra)
target_link_libraries(bench_parser PRIVATE minmea tinygpsplus libnmea)

# Бенчмарк nav_gen
add_executable(bench_nav_gen bench_nav_gen.cpp)
target_compile_options(bench_nav_gen PRIVATE -Wall -Wextra)

# Вывод информации
message(STATUS "Building LightNMEA benchmarks with C++17")
message(STATUS "Optimization: ${CMAKE_CXX_FLAGS_RELEASE}")
