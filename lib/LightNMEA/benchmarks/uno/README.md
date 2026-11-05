# LightNMEA AVR benchmark — Arduino UNO R3 (ATmega328P + CH340G)

Сравнительный бенчмарк NMEA-парсеров прямо на 8-битном AVR:
[LightNMEA](../../include/light_nmea.hpp) vs [minmea](../firmware/lib/minmea) vs
[TinyGPSPlus](../firmware/lib/TinyGPSPlus).

Это переписанная версия бенчмарка из [`../avr/`](../avr/) (ATmega32 + AVRISP mkII)
под штатную плату **Arduino UNO R3 (ATmega328P @ 16 МГц, USB-мост CH340G)**.

## Почему это больше не «кирпичит» плату

Прошлый бенч прошивался через ISP-программатор со сменой фьюзов — при неудачной
записи `lfuse` чип переставал отвечать (см.
[`../avr/brick-recovery.md`](../avr/brick-recovery.md)). Здесь это исключено
архитектурно:

- прошивка идёт через **on-board Optiboot bootloader** по UART
  (`avrdude -c arduino -b 115200`) — тот же путь, что и у Arduino IDE;
- **фьюзы никогда не записываются** — в действии `check` они только читаются;
- программатор не нужен, достаточно USB-кабеля.

## Требования

- `avr-gcc >= 9` (C++17), `avr-libc`, `avrdude >= 6.3`;
- плата UNO R3 (ATmega328P), подключённая по USB (CH340G → `/dev/ttyUSB0`);
- при отсутствии libstdc++ в тулчейне используются freestanding-заглушки
  из [`../avr/src/stdcxx/`](../avr/src/stdcxx/) — подключаются автоматически.

## Использование

```bash
./build.sh                 # собрать (10000 пакетов/проход, -O2)
./build.sh check           # проверить плату: сигнатура + фьюзы (read-only)
./build.sh flash           # собрать + прошить через Optiboot (фьюзы не трогаем)
./build.sh monitor         # монитор порта; DTR-ресет перезапускает бенч
./build.sh run             # прошить + снять вывод бенча одной командой
BENCH_PACKET_COUNT=30000 ./build.sh flash
OPT=-Os ./build.sh flash
PORT=/dev/ttyUSB1 ./build.sh check
./build.sh clean
```

Результаты печатаются в UART (PD1 → CH340G → USB) один раз после каждой
прошивки/ресета. Повторный запуск — просто переоткройте последовательный порт
(открытие порта дёргает DTR → аппаратный ресет) или нажмите кнопку RESET.

Метрики: `pkt/s`, `ns/pkt`, `cycles/pkt`, `pkt/s/MHz` (best-of-3 проходов),
как на 32-битных платах в [`../firmware/`](../firmware/).

## Результаты (ATmega328P @ 16 МГц, 10000 пакетов/проход, best-of-3)

```
Implementation  pkt/s       ns/pkt  cycles/pkt  pkt/s/MHz  ok/total
LightNMEA         2441   409600       6554        152 10000/10000
minmea             818  1222246      19556         51 10000/10000
TinyGPSPlus        905  1104281      17668         56 10000/10000
```

- **LightNMEA быстрее minmea в ~3.0 раза** и TinyGPSPlus в ~2.7 раза;
- LightNMEA — единственный парсер с запасом для реального GPS-потока 9600 бод
  (~90 предложений/с): 2441 pkt/s покрывают его с ~27-кратным запасом;
- footprint: flash 21 126 Б из 32 768, SRAM 554 Б из 2048 (вместе со всеми
  тремя парсерами).

## Отличия от бенча ATmega32 ([`../avr/`](../avr/))

| | ATmega32 (../avr/) | UNO R3 (../uno/) |
|---|---|---|
| MCU | ATmega32L-8AU @ 8 МГц | ATmega328P @ 16 МГц |
| Прошивка | AVRISP mkII (ISP, риск фьюзов) | Optiboot по UART (`-c arduino`) |
| UART | USART0 (UCSRA/UBRR/UDR) | USART0 (UCSR0A/UBRR0/UDR0) |
| Timer1 | `TIMSK`/`TIFR` | `TIMSK1`/`TIFR1` |
| Тик таймера | 1 мкс @ 8 МГц | 0.5 мкс @ 16 МГц (учтено в `TIMER_TICK_NS`) |
| Методика | идентична: PROGMEM-зеркало пакетов → SRAM-пул → best-of-N | идентична |

При переносе также исправил два бага, унаследованные от 8-МГц версии:
- расчёт `TIMER_TICK_US` был верен только при `F_CPU/8 = 1 МГц`; на 16 МГц
  интервалы завышались в 4 раза (заменено на `TIMER_TICK_NS = 8e9/F_CPU`);
- `ns_per_pkt = dt*1000/COUNT` переполнял `uint32` при проходах длиннее ~1.2 с
  (переведён в 64-битную арифметику).
