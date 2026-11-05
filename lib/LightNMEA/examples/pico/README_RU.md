# LightNMEA + Raspberry Pi Pico (RP2040)

Пример для Raspberry Pi Pico (RP2040, Cortex-M0+ @ 133 МГц) с GNSS-модулем
**G10F-F30**, подключённым к **GP0/GP1 (UART0)**:

| Пин Pico | Сигнал | Модуль GNSS |
|----------|--------|-------------|
| GP0 (UART0 TX) | команды (опционально) | RX |
| GP1 (UART0 RX) | поток NMEA-0183 | TX |
| 3V3 / GND | питание | VCC / GND |

**Нет приёма спутников?** В помещении фикса не будет, а без модуля UART вообще
молчит. Для таких случаев в прошивке есть **демо-режим** (`[env:pico_demo]`):
встроенный синтетический NMEA-трек подаётся через тот же конвейер
`Stream -> NMEAStreamReader -> Parser` — модуль и спутники не нужны.

Распознанные данные печатаются в **USB CDC** (порт «Pico» на хосте), поэтому
UART0 полностью остаётся под GNSS. Светодиод на плате (GP25) переключается на
каждом принятом пакете и гаснет, если данных нет дольше 5 секунд.

## Требования

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/) 6.x
  (платформа `raspberrypi` и Arduino-ядро earlephilhower скачаются
  автоматически при первой сборке).

## Сборка и прошивка

```console
cd lib/LightNMEA/examples/pico
pio run -e pico                # собрать (реальный режим: GNSS на UART0)
pio run -e pico -t upload      # прошить (picotool сам уводит плату в BOOTSEL,
                               # кнопку держать не нужно)
pio run -e pico_demo -t upload # демо-режим: встроенный трек, модуль не используется
```

Если `picotool` не смог перехватить работающее приложение — зажмите **BOOTSEL**
при подключении платы и повторите прошивку.

## Монитор

```console
pio device monitor             # 115200, USB CDC
```

После каждого ресета прошивка печатает банер (ждёт подключения терминала до
4 секунд), затем непрерывно выводит распознанные позиции:

```
=== LightNMEA Pico example (G10F-F30 on GP0/GP1) ===
GNSS UART0: 9600 baud, TX GP0 / RX GP1
Fix: Yes
satellites: 8
latitude: 48.117300
longitude: 11.516667
...
```

Если появилось `no GNSS data for 5 s (check wiring / baud rate)`, проверьте:

- распиновку: TX модуля должен идти на **GP1**, а не на GP0;
- скорость: у G10F-F30 по умолчанию **38400 бод** (уже задано в
  `platformio.ini`); переопределите флагом `pio run -e pico -DGNSS_BAUD=9600`
  (или 115200), если модуль настроен иначе;
- питание: модулю нужен чистый 3.3 В, первая фиксация может занять от
  нескольких секунд до минуты.

## Конфигурация (build-флаги)

| Флаг | По умолчанию | Значение |
|------|--------------|----------|
| `GNSS_BAUD` | 38400 | скорость UART0 GNSS-модуля |
| `GNSS_TX_PIN` | 0 | GP-пин UART0 TX (к RX модуля) |
| `GNSS_RX_PIN` | 1 | GP-пин UART0 RX (от TX модуля) |
| `LED_PIN` | 25 | светодиод активности |

## Как это работает

1. `Serial1` (UART0) принимает поток NMEA-0183 от модуля.
2. `GnssStream` реализует абстрактный `light_nmea::Stream::read()` поверх
   `Serial1` (неблокирующий, выгребает всё из буфера драйвера).
3. `reader.read_available(parser, stream, get_time_ms)` собирает NMEA-предложения
   (анти-спам фильтр на `millis()`) и передаёт их в `light_nmea::Parser`.
4. `light_nmea::to_txt()` форматирует фикс; `Serial` (USB CDC) печатает его.

Примечание: в Arduino есть собственный класс `Stream`, поэтому в примере типы
LightNMEA всегда пишутся с квалификацией (`light_nmea::Parser`,
`light_nmea::Stream`, ...) — без `using namespace light_nmea`.
