# LightNMEA RTOS benchmarks (FreeRTOS & Zephyr)

Сравнительный бенчмарк NMEA-парсеров под ОСРВ: **LightNMEA** vs **minmea** vs
**TinyGPSPlus** на идентичных пакетах из `nav_gen::NAV_PACKETS` (RMC/GGA/VTG/GLL
от talker'ов GP/GN). Это RTOS-адаптация
[`benchmarks/firmware/src/bench_compare_mcu.cpp`](../firmware/src/bench_compare_mcu.cpp): горячие циклы и логика замера перенесены без изменений, заменены только Arduino-HAL (`micros()`, `Serial`) на API ОСРВ.

## Структура

```
rtos/
├── bench_compare_rtos.hpp   # общее портируемое ядро (горячие циклы, best-of-N,
│                            #   метрики). Не зависит от ОСРВ: таймер и вывод
│                            #   встраиваются в BenchEnv.
├── Arduino.h                # минимальная обёртка Arduino для сборки TinyGPSPlus
│                            #   (millis() определяется в обёртке ОСРВ).
├── freertos/                # FreeRTOS-обёртка (POSIX-порт, хост)
├── zephyr/                  # Zephyr-обёртка (native_sim / nRF52840 DK)
└── esp32/                   # ESP32 D1 mini (FreeRTOS, ESP-IDF/Arduino)
```

Конкуренты (minmea, TinyGPSPlus) берутся из vendored-копий
[`benchmarks/firmware/lib`](../firmware/lib), синхронных с
[`benchmarks/compare/third_party`](../compare/third_party). В `esp32/` они
лежат прямо в `src/` (PlatformIO компилирует безусловно всё из `src/`, а LDF не прослеживает include по относительному пути к общему ядру).

## Метрики

- **pkt/s** - пропускная способность (пакетов в секунду);
- **ns/pkt** - наносекунды на пакет;
- **cycles/pkt** - циклы на пакет (от `F_CPU`);
- **pkt/s/MHz** - pkt/s на 1 МГц тактовой частоты;
- **ok/total** - доля успешно распознанных пакетов.

Best-of-N прогонов (по умолчанию 3). Конфигурация через build flags:
`BENCH_PACKET_COUNT` (по умолчанию 30000) и `BENCH_PASSES` (по умолчанию 3).

## FreeRTOS (хост, POSIX-порт)

```console
cd lib/LightNMEA/benchmarks/rtos/freertos
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/freertos_nmea_bench
```

Пример вывода:

```
=== LightNMEA RTOS comparative benchmark ===
Packets/pass: 30000, passes: 3 (best), F_CPU: 100000000 Hz

Implementation  pkt/s       ns/pkt  cycles/pkt  pkt/s/MHz  ok/total
LightNMEA         7816571      127         13      78165  30000/30000
minmea            2989238      334         33      29892  30000/30000
TinyGPSPlus       2705627      369         37      27056  30000/30000
```

## Zephyr

```console
cd lib/LightNMEA/benchmarks/rtos/zephyr
west build -b native_sim -d build/native_sim
./build/native_sim/zephyr/zephyr.exe
```

Реальная плата (проверено: nRF52840 DK):

```console
west build -b nrf52840dk/nrf52840 -d build/nrf52840dk
west flash -d build/nrf52840dk
```

Для `native_sim` нужны 32-битные библиотеки хоста:
`sudo apt install gcc-multilib g++-multilib` (как и для примера
[`examples/zephyr`](../../examples/zephyr/README_RU.md)).

## ESP32 D1 mini (FreeRTOS через ESP-IDF/Arduino)

```console
cd lib/LightNMEA/benchmarks/rtos/esp32
pio run -t upload -e d1_mini32
pio device monitor -b 115200
```

Бенчмарк запускается в задаче FreeRTOS (`xTaskCreate`, приоритет 2 - выше
loopTask, чтобы замер не прерывался), время - `esp_timer_get_time()`
(аппаратный таймер), вывод - `printf` (перенаправлен на Serial UART0).
Task WDT отключён (`disableCore0WDT()`/`disableCore1WDT()`): бенчмарк -
длительная задача для CPU, иначе IDLE-задача не успевает сбрасывать
watchdog (по умолчанию 5 с).

## Как это работает

1. Обёртка ОСРВ создаёт `bench_rtos::BenchEnv` с двумя функциями:
   - `now_us()` - источник времени в микросекундах;
   - `print()` - printf-совместимый вывод.
2. `bench_rtos::run_benchmark()` прогоняет три парсера на одном и том же
   наборе пакетов (best-of-N) и печатает таблицу.
3. Горячие циклы не используют heap и FPU — работают на Cortex-M0+ и RISC-V.

Источники времени:

| ОСРВ | now_us() | Примечание |
|---|---|---|
| FreeRTOS (POSIX-порт) | `clock_gettime(CLOCK_MONOTONIC)` | точность до мкс |
| Zephyr | `k_cyc_to_us_ceil32(k_cycle_get_32())` | аппаратный цикловой счётчик |

## Перенос на другое железо

- **FreeRTOS на МК:** замените `now_us()` на цикловой счётчик платформы
  (Cortex-M: `DWT->CYCCNT`; ESP32: `esp_timer_get_time()`; RISC-V: `mcycle`),
  укажите свой `FreeRTOSConfig.h` и порт вместо POSIX.
- **Zephyr:** `k_cycle_get_32()` уже использует аппаратный цикловой счётчик.
  `F_CPU` по умолчанию равен `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC`; для
  nRF52840 (системный таймер - RTC 32.768 кГц) в `CMakeLists.txt` задан
  `F_CPU=64000000` (частота CPU), чтобы метрика cycles/pkt была осмысленной.
  Для другой платы задайте свой `F_CPU` через `target_compile_definitions`.

## Сравнение с firmware-бенчмарком

[`benchmarks/firmware`](../firmware) - тот же замер, но под Arduino (PlatformIO) на Pico/ESP32/STM32 и др. RTOS-версия отличается только источником времени и выводом; метрики и горячие циклы идентичны, поэтому результаты сопоставимы.

### Замер на ESP32 D1 mini (240 МГц, 30000 пакетов, best-of-3)

| Парсер | RTOS (FreeRTOS) pkt/s | firmware (Arduino) pkt/s | Разница |
|---|---|---|---|
| LightNMEA | 110 897 | 110 629 | +0.24% |
| minmea | 34 000 | 33 955 | +0.13% |
| TinyGPSPlus | 34 810 | 33 882 | +2.7% |

Результаты совпадают с учетом небольшого разброса: контекст задачи FreeRTOS и
`esp_timer_get_time()` не добавляют(!) измеримых накладных расходов в горячий цикл. Все 30000/30000 пакетов распознаны в обоих случаях.