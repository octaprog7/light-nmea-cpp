# AVR Benchmark — LightNMEA

On-target бенчмарк парсера NMEA-сентенций для классического 8-битного AVR (ATmega32L-8AU).

> **Актуальная версия:** для Arduino UNO R3 (ATmega328P @ 16 МГц, CH340G) бенч
> переписан и переехал в [`../uno/`](../uno/) — прошивка через Optiboot по UART,
> без ISP-программатора и без записи фьюзов (см. [brick-recovery.md](brick-recovery.md)).
> Здесь методика/тулчейн для ATmega32 через AVRISP mkII сохранена для справки.

## Варианты бенчмарков

| Вариант | Описание | Метрика |
|---------|----------|---------|
| `avr` (по умолчанию) | Полный сравнительный: LightNMEA + minmea + TinyGPSPlus | pkt/s, ns/pkt, cycles/pkt, pkt/s/MHz |
| `simple` | Только LightNMEA | pkt/s, ns/pkt, pkt/s/MHz |

## Быстрый старт

```bash
cd lib/LightNMEA/benchmarks/avr

# Собрать простой бенч (только LightNMEA)
BENCH=simple ./build.sh

# Собрать полный сравнительный бенч
./build.sh

# Собрать под 16 МГц
F_CPU=16000000 ./build.sh

# Изменить количество пакетов на проход
BENCH_PACKET_COUNT=30000 ./build.sh

# Оптимизация по размеру
OPT=-Os ./build.sh

# Прошить HEX через AVRISP mkII
./build.sh flash

# Прочитать сигнатуру и фьюзы (read-only)
./build.sh fuses

# Мониторинг UART (нужен USB-UART на PD0/PD1)
./build.sh monitor

# Очистка
./build.sh clean
```

## Аппаратная конфигурация

### Подключение ISP (AVRISP mkII → плата)

| AVRISP mkII | ATmega32L |
|-------------|-----------|
| Pin 1 (MISO) | PB6 (MISO, pin 17) |
| Pin 2 (NC) | — |
| Pin 3 (SCK) | PB7 (SCK, pin 19) |
| Pin 4 (MOSI) | PB5 (MOSI, pin 18) |
| Pin 5 (RST) | PB4 (RESET, pin 1) |
| Pin 6 (GND) | GND (pin 10) |

> **Важно:** AVRISP mkII **не питает** целевую плату по ISP. Необходимо отдельное питание 5V (VCC pin 22, GND pin 10/11/21).

### Выход данных (USART0)

| Платa | AVRISP mkII | USB-UART |
|-------|-------------|----------|
| PD1 (TX, pin 2) | — | RX |
| PD0 (RX, pin 3) | — | TX (не используется) |
| GND | GND | GND |

**Параметры UART:**
- 8N1 (8 бит, нет чётности, 1 стоповый)
- Автовыбор бода:
  - 8 МГц → 38400 бод (ошибка 0.16%)
  - 16 МГц → 115200 бод

## Результаты сборки

### bench_simple.cpp (только LightNMEA)
```
   text	   data	    bss	    dec	    hex	filename
  13270	      0	    552	  13822	   35fe	build/bench_simple.elf
=== OK: flash 13270 B, SRAM 552 B ===
```

### bench_avr.cpp (полный сравнительный)
```
   text	   data	    bss	    dec	    hex	filename
  20816	      0	    554	  21370	   537a	build/bench_avr.elf
=== OK: flash 20816 B, SRAM 554 B ===
```

## Архитектура

### Проблема PROGMEM
На классическом AVR (avr5, ATmega32) constexpr/const-данные лежат во flash, но динамическая индексация через указатели (`std::string_view` / `std::array`) генерирует LD (чтение SRAM), а не LPM (чтение flash). Данные читались бы неверно.

### Решение: PROGMEM-пул → SRAM-копия
1. Пакеты хранятся во flash как `PROGMEM`-массивы
2. При старте копируются в SRAM-пул (`g_pool[512]`)
3. `std::string_view` указывают в SRAM-копии
4. Парсеры работают с обычным SRAM-буфером, как при реальном приёме из UART

### Timer1 для micros()
- Прескалер /8
- Тик = max(1, F_CPU/8e6) мкс: 1.0 мкс @8 МГц, 2.0 мкс @16 МГц
- 32-битное расширение через ISR (суммирует переполнения TCNT1)

## Freestanding STL

Так как тулчейн avr-gcc без libstdc++, используются заглушки в `src/stdcxx/`:
- `<cstddef>` — `size_t`, `ptrdiff_t`
- `<cstdint>` — `int/uintN_t`
- `<type_traits>` — `integral_constant`, `is_enum`, `underlying_type_t`, `enable_if`, `move`, `forward`
- `<array>` — `std::array<T, N>` с полным API
- `<string_view>` — `std::string_view` с ctor, size, empty, operator[], data, substr

## Ожидаемые результаты

На ATmega32 @ 8 МГц (ожидаемо):
- LightNMEA: ~500-1500 pkt/s (зависит от состава пакетов)
- ~0.06-0.20 мкс на пакет

Для сравнения (RP2040 @ 133 МГц):
- LightNMEA: ~150'000-300'000 pkt/s

## Реальные результаты

### ATmega32L @ 1 МГц (внутренний осциллятор)

```
=== LightNMEA MCU comparative benchmark (8-bit AVR) ===
Packets/pass: 10000, passes: 3 (best), F_CPU: 8000000 Hz, UART baud: 9600

Implementation  pkt/s       ns/pkt  cycles/pkt  pkt/s/MHz  ok/total
LightNMEA         1230   383149       3065        153 10000/10000
minmea             407   303562       2428         50 10000/10000
TinyGPSPlus        567    44931        359         70 10000/10000
```

**Ключевые выводы:**
- LightNMEA в **3 раза быстрее minmea** (1230 vs 407 pkt/s)
- LightNMEA в **2.2 раза быстрее TinyGPSPlus** (1230 vs 567 pkt/s)
- Лучшая эффективность **153 pkt/s/MHz** среди всех парсеров

### Сравнение с 32-битными MCU (pkt/s/MHz)

| MCU | Частота | pkt/s | pkt/s/MHz |
|-----|---------|-------|-----------|
| **ATmega32L** | **8 МГц** | **1230** | **153** |
| ESP32 | 240 МГц | ~150000 | ~625 |
| STM32F1 | 168 МГц | ~100000 | ~595 |
| RP2040 | 133 МГц | ~80000 | ~600 |

ATmega32L уступает 32-битным MCU в ~4 раза по pkt/s/MHz, но при этом имеет значительно меньшую стоимость и энергопотребление.

## Известные проблемы

### Окирпиченный чип ATmega32L

После изменения `lfuse` с `0xFF` (внешний кварц) на `0xE0` (внутренний осциллятор) чип перестал отвечать через ISP.

**Причина:** AVRISP mkII не может установить ISP-связь при неправильной частоте чипа.

**Решение:** Полная инструкция по восстановлению — [brick-recovery.md](brick-recovery.md)

**Быстрый фикс:**
1. Купить USBasp программатор (~300 руб)
2. Восстановить `lfuse`: `avrdude -p atmega32 -c usbasp -F -U lfuse:w:0xFF:m`
3. Прошить обратно: `avrdude -p atmega32 -c usbasp -F -U flash:w:bench_avr.hex:i`