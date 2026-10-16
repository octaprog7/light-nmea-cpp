cmake_minimum_required(VERSION 3.16)
project(light-nmea-cpp VERSION 1.0.0 LANGUAGES C CXX)

# Отключение исключений и RTTI (опционально). Флаги - только для C++,
# чтобы не сыпать предупреждениями при компиляции C-файлов (C API).
option(DISABLE_EXCEPTIONS "Disable C++ exceptions" ON)
option(DISABLE_RTTI "Disable C++ RTTI" ON)

if(DISABLE_EXCEPTIONS)
    if(MSVC)
        add_compile_options(/EHs-c-)
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    endif()
    message(STATUS "C++ exceptions disabled")
endif()

if(DISABLE_RTTI)
    if(MSVC)
        add_compile_options(/GR-)
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    endif()
    message(STATUS "C++ RTTI disabled")
endif()

# Стандарт C++20 (можно переключить на C++17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Генерация compile_commands.json для language server
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Оптимизации для релиза (GCC/Clang и MSVC)
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto")
endif()

# Header-only библиотека
add_library(light_nmea INTERFACE)
target_include_directories(light_nmea INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# C API: компилируемая обёртка для проектов на Си.
# Ядро остаётся header-only; эту цель линкуют только те, кому нужен C API.
add_library(light_nmea_c STATIC src/lightnmea_c.cpp)
target_include_directories(light_nmea_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(light_nmea_c PRIVATE light_nmea)

# Тесты
add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE light_nmea)

# Включаем тестирование
enable_testing()
add_test(NAME ParserTests COMMAND test_parser)

# Бенчмарк с nav_gen. bench_parser требует сторонние
# реализации (minmea/TinyGPSPlus/libnmea) и собирается отдельным проектом:
#   benchmarks/build.sh  (см. benchmarks/CMakeLists.txt)
add_executable(bench_nav_gen benchmarks/bench_nav_gen.cpp)
target_link_libraries(bench_nav_gen PRIVATE light_nmea)

# Пример использования NMEAStreamReader (UART-подобный + файл)
add_executable(stream_reader_example examples/stream_reader_example.cpp)
target_link_libraries(stream_reader_example PRIVATE light_nmea)

# Примеры для POSIX (TCP-поток и последовательный порт /dev/ttyXXX)
if(UNIX)
    add_executable(tcp_stream_example examples/tcp_stream_example.cpp)
    target_link_libraries(tcp_stream_example PRIVATE light_nmea)

    add_executable(serial_reader_example examples/serial_reader_example.cpp)
    target_link_libraries(serial_reader_example PRIVATE light_nmea)

    # Linux/POSIX демон: SerialStream -> NMEAStreamReader -> Parser -> CSV
    add_executable(gps_daemon examples/gps_daemon.cpp)
    target_link_libraries(gps_daemon PRIVATE light_nmea)

    # GNSS Dashboard (TUI): SerialPort -> SerialParser (CSV / NMEA-0183 /
    # SYS_MSG) -> curses. Требуется ncursesw (пакет libncurses-dev).
    find_library(GNSS_NCURSESW_LIB NAMES ncursesw)
    find_path(GNSS_NCURSESW_INCLUDE NAMES ncursesw/curses.h)
    if(GNSS_NCURSESW_LIB AND GNSS_NCURSESW_INCLUDE)
        add_executable(gnss_dashboard examples/gnss_dashboard.cpp)
        target_link_libraries(gnss_dashboard PRIVATE light_nmea ${GNSS_NCURSESW_LIB})
        target_include_directories(gnss_dashboard PRIVATE ${GNSS_NCURSESW_INCLUDE})
        target_compile_definitions(gnss_dashboard PRIVATE _XOPEN_SOURCE_EXTENDED=1)
        message(STATUS "gnss_dashboard: built with ncursesw")
    else()
        message(STATUS "gnss_dashboard: skipped (ncursesw not found; run: sudo apt install libncurses-dev)")
    endif()
endif()

# Unit-тест для NMEAStreamReader
add_executable(test_stream_reader tests/test_stream_reader.cpp)
target_link_libraries(test_stream_reader PRIVATE light_nmea)
add_test(NAME StreamReaderTests COMMAND test_stream_reader)

# Unit-тест для функций форматирования
add_executable(test_formatter tests/test_formatter.cpp)
target_link_libraries(test_formatter PRIVATE light_nmea)
add_test(NAME FormatterTests COMMAND test_formatter)

# C API: пример (Cи) и unit-тест
add_executable(c_api_example examples/c_api_example.c)
target_link_libraries(c_api_example PRIVATE light_nmea_c)

add_executable(test_c_api tests/test_c_api.c)
target_link_libraries(test_c_api PRIVATE light_nmea_c)
add_test(NAME CApiTests COMMAND test_c_api)