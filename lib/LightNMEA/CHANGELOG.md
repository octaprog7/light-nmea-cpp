# Changelog

Содержит крупные изменения проекта.

Формат основан на [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
проект придерживается [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-11-05

### Added

- **C API** ([`include/lightnmea_c.h`](include/lightnmea_c.h),
  [`src/lightnmea_c.cpp`](src/lightnmea_c.cpp)) — обёртка для проектов на чистом C:
  `lightnmea_create/parse_line/get_data/destroy`, плоская структура
  `lightnmea_data_t` с целочисленными raw-полями. Пример:
  [`examples/c_api_example.c`](examples/c_api_example.c), тест:
  [`tests/test_c_api.c`](tests/test_c_api.c).
- **Single-header** вариант ([`lightnmea_single.hpp`](lightnmea_single.hpp),
  генерация: [`scripts/amalgamate.py`](scripts/amalgamate.py)).
- **ESP-IDF компонент** ([`esp-idf-component/`](esp-idf-component/)) — использование
  без Arduino.
- **RTOS-бенчмарки** ([`benchmarks/rtos/`](benchmarks/rtos/)): FreeRTOS (POSIX-порт),
  Zephyr (native_sim / nRF52840 DK), ESP32 D1 mini (FreeRTOS + ESP-IDF/Arduino).
- **CI** (GitHub Actions): сборка и тесты CMake (Linux/macOS/Windows),
  компиляция PlatformIO-прошивок, Arduino-скетчей, FreeRTOS/Zephyr,
  CodeQL, фаззинг + санитайзеры.
- **Doxygen** API reference ([`Doxyfile`](Doxyfile)) + деплой на GitHub Pages.
- **Документация для контрибьюторов**: [`CONTRIBUTING.md`](CONTRIBUTING.md),
  [`CHANGELOG.md`](CHANGELOG.md), шаблоны issues.
- **clang-format / clang-tidy** конфигурация (`.clang-format`, `.clang-tidy`).
- CMakeLists: поддержка MSVC (флаги исключений/RTTI/оптимизаций).
- **Пример RT-Thread** ([`examples/rtthread/`](examples/rtthread/)): чтение NMEA
  из `rt_device` (прерывание RX + семафор `rx_indicate`), кольцевой буфер,
  `NMEAStreamReader` + `Parser`; режим симуляции для QEMU (`SConscript`, `Kconfig`).
- **Пример Raspberry Pi Pico** ([`examples/pico/`](examples/pico/)): GNSS-модуль
  G10F-F30 на UART0 (GP0/GP1, 38400 бод), вывод в USB CDC, демо-режим со
  встроенным NMEA-треком для проверки без спутников (PlatformIO, env
  `pico` / `pico_demo`). Проверен на живом приёме (fix, 11 спутников).
- **AVR-бенчмарки**: ATmega32 через AVRISP mkII ([`benchmarks/avr/`](benchmarks/avr/),
  включая [`brick-recovery.md`](benchmarks/avr/brick-recovery.md)) и переписанная
  версия для Arduino UNO R3 / ATmega328P ([`benchmarks/uno/`](benchmarks/uno/)) —
  прошивка через Optiboot без записи фьюзов. Результаты на ATmega328P @ 16 МГц:
  LightNMEA 2441 pkt/s (в ~3 раза быстрее minmea, в ~2.7 раза быстрее TinyGPSPlus).

### Changed

- Проверка утечек памяти valgrind memcheck (модульные тесты + фаззинг,
  50 000 мутаций) — ошибок и утечек нет; строка «пропущено (не установлен)»
  в таблице качества удалена.

## [1.0.0] - 2026

### Added

- Header-only C++17 NMEA-парсер: RMC, GGA, VTG, GLL.
- Целочисленная модель данных (без FPU): координаты 1e-7°, курс 1e-2°, скорость x1000.
- Поддержка Arduino IDE, PlatformIO, CMake.
- Примеры: BasicGPS, GPSToJSON, GPSLogger (Arduino); stream/serial/tcp reader,
  gps_daemon, GNSS dashboard (POSIX).
- Бенчмарки: bench_parser (параметрический), bench_nav_gen, сравнительный
  firmware-бенчмарк (Pico/ESP32/STM32), сравнение с minmea/TinyGPSPlus/libnmea.
- Фаззинг-тест парсера ([`tests/fuzz_parser.cpp`](tests/fuzz_parser.cpp)).
