# Contributing to LightNMEA

Спасибо за интерес к проекту! Ниже — правила и рекомендации для соавторов кода.

## Быстрый старт

1. Форкните репозиторий и создайте ветку от `main`:
   `git checkout -b feature/my-feature`.
2. Внесите изменения.
3. Убедитесь, что всё 'проходит':
   ```console
   cd lib/LightNMEA
   ./scripts/run_quality_checks.sh   # компиляция, unit-тесты, фаззинг, санитайзеры
   cmake -S . -B build && cmake --build build -j && ctest --test-dir build
   ```
4. Откройте Pull Request с описанием изменений.

## Что проверяет CI

GitHub Actions (`.github/workflows/ci.yml`) автоматически проверяет:

- сборку и unit-тесты на Linux/macOS/Windows (`-Wall -Wextra -Werror`);
- компиляцию PlatformIO-прошивок (Pico, ESP32) и RTOS-бенчмарка ESP32;
- компиляцию Arduino-скетчей (BasicGPS/GPSToJSON/GPSLogger);
- сборку FreeRTOS-примера и бенчмарка (POSIX-порт);
- сборку Zephyr-примера (native_sim);
- фаззинг и санитайзеры (`scripts/run_quality_checks.sh`);
- CodeQL статический анализ.

## Стиль кода

- C++17 (ядро не зависит от Arduino/POSIX).
- Форматирование: `clang-format` (см. `.clang-format`).
- Статический анализ: `clang-tidy` (см. `.clang-tidy`).
- Заголовки документируются Doxygen-комментариями (`@brief`, `@param`, `@return`).
- Новые элементы публичного API сопровождаются unit-тестами в `tests/`.

## Структура репозитория

```
lib/LightNMEA/
├── include/          # публичные заголовки (header-only ядро)
├── src/              # компилируемые части (C API, Arduino-обёртка)
├── examples/         # примеры (Arduino, POSIX, FreeRTOS, Zephyr)
├── tests/            # unit-тесты и фаззинг
├── benchmarks/       # бенчмарки (host, firmware, RTOS)
├── scripts/          # вспомогательные скрипты
└── esp-idf-component/ # компонент для ESP-IDF
```

## Лицензия

Проект распространяется под GPL-3.0 (см. `LICENSE`). Внося изменения, вы
соглашаетесь с тем, что они будут опубликованы под той же лицензией.