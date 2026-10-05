cmake_minimum_required(VERSION 3.16)
project(light-nmea-cpp VERSION 1.0.0 LANGUAGES CXX)

# Отключение исключений и RTTI (опционально)
option(DISABLE_EXCEPTIONS "Disable C++ exceptions" ON)
option(DISABLE_RTTI "Disable C++ RTTI" ON)

if(DISABLE_EXCEPTIONS)
    add_compile_options(-fno-exceptions)
    message(STATUS "C++ exceptions disabled")
endif()

if(DISABLE_RTTI)
    add_compile_options(-fno-rtti)
    message(STATUS "C++ RTTI disabled")
endif()

# Стандарт C++20 (можно переключить на C++17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Генерация compile_commands.json для language server
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Оптимизации для релиза
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto")

# Header-only библиотека
add_library(light_nmea INTERFACE)
target_include_directories(light_nmea INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Тесты
add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE light_nmea)

# Бенчмарк
add_executable(bench_parser benchmarks/bench_parser.cpp)
target_link_libraries(bench_parser PRIVATE light_nmea)

# Включаем тестирование
enable_testing()
add_test(NAME ParserTests COMMAND test_parser)

# Бенчмарк с nav_gen
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
endif()

# Unit-тест для NMEAStreamReader
add_executable(test_stream_reader tests/test_stream_reader.cpp)
target_link_libraries(test_stream_reader PRIVATE light_nmea)
add_test(NAME StreamReaderTests COMMAND test_stream_reader)

# Unit-тест для функций форматирования
add_executable(test_formatter tests/test_formatter.cpp)
target_link_libraries(test_formatter PRIVATE light_nmea)