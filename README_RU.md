# light-nmea-cpp

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Language](https://img.shields.io/badge/C%2B%2B-17-yellowgreen)
![Version](https://img.shields.io/badge/version-1.0.0-orange)
![Platform](https://img.shields.io/badge/platform-embedded%20%7C%20desktop-lightgrey)

[**English (README.md)**](README.md) | **Русский**

Сверхбыстрый NMEA 0183 GPS-парсер с **нулевым выделением памяти**, написанный на современном C++ (**базовый стандарт — C++17; те же исходники собираются и как C++20**).

Библиотека ориентирована на **32-битные микроконтроллеры** (Cortex-M, ESP32, STM32, RP2040): целочисленный конвейер, который **ни разу не обращается к FPU**, и **никакого выделения блоков из RAM на горячем пути** — скорость разбора максимально близка к физическому пределу железа. При этом она работает в высокочастотных телеметрических конвейерах на десктопе — и готова для 8-битных AVR-плат, как только ваш тулчейн достигнет требуемого стандарта C++ (см. примечание для пользователей 8-битных плат ниже).

## Мотивация

После выпуска **[light-nmea-micropython](https://github.com/octaprog7/light-nmea-micropython)** мы обнаружили, что ниша C++ остаётся практически открытой. Большинство NMEA-парсеров написано под Python либо рассчитано на старый C эпохи Arduino, оставляя современных C++-разработчиков без по-настоящему быстрого варианта с нулевым выделением памяти.

Задача была поставлена для **32-битных микроконтроллеров**: достичь производительности максимально близко к практически возможному пределу **без использования FPU** — чистая целочисленная арифметика от токенизации предложений до координатной математики и форматирования. И **никакого выделения блоков из RAM на горячем пути**: во время разбора библиотека никогда не обращается к куче, поэтому её потребление памяти строго ограничено на этапе компиляции и вписывается в любой современный МК.

light-nmea-cpp закрывает этот пробел. Её архитектура и базовая логика разбора напрямую унаследованы от **light-nmea-micropython** — проверенного в реальных проектах — и переписаны на современном C++ (базовый стандарт — C++17, дружелюбны к C++20). Она приносит самый быстрый разбор NMEA тем, кто пишет на C++ профессионально, — и кто не променяет свой тулчейн на Python-скрипт. Никаких выделений в рантайме, никакой плавающей точки на горячем пути — только скорость разбора там, где она нужна: на 32-битном микроконтроллере или в высокочастотном телеметрическом конвейере.

## Что вы получаете

**Никогда не узкое место.** При 5–14 миллионах предложений в секунду разбор перестаёт быть затратой, о которой стоит думать. На 32-битном МК парсер работает в десятки и сотни раз быстрее, чем GPS-модуль способен его кормить, — ваша прошивка тратит такты на приложение, а не на NMEA.

**Никаких сбоев памяти.** Нулевое выделение кучи на горячем пути означает отсутствие ошибок `malloc`, фрагментации и перезагрузок из-за нехватки памяти — даже на устройствах, которым предстоит работать месяцами или годами. Использование памяти полностью известно на этапе компиляции.

**Вписывается куда угодно.** ~1.3 КБ Flash и 52 байта RAM позволяют парсеру сосуществовать с полноценным приложением на маленьком МК — или вовсе затеряться внутри большого.

**Детерминированность по дизайну.** Ограниченные буферы, без рекурсии, без скрытого состояния. Одинаковый вход всегда даёт одинаковый выход с одинаковой задержкой — предсказуемое поведение, на которое можно полагаться в системах реального времени.

**Компилятор охраняет вашу математику.** Координаты, скорости и курсы — это строгие типы, а не голые float: смешивание узлов с км/ч становится ошибкой компиляции, а не незаметным багом. Целочисленное хранение также означает идентичные результаты на всех платформах — никаких сюрпризов от округления float.

**Ноль настройки.** Один заголовок — включай и работай: для Arduino, PlatformIO, CMake — чего бы вы ни использовали.

**Готовый вывод из коробки.** Включены форматтеры TXT, CSV, JSON и Compact, плюс конвертация единиц — вам не придётся изобретать вывод для логирования или телеметрии.

**От AVR до x86_64.** Один и тот же исходник работает на 8-битном AVR, Cortex-M, ESP32 и десктопном сервере. Пишите один раз — запускайте где угодно.

## Возможности

- **Только заголовки (header-only)** — подключил и работаешь, без шага компиляции и зависимостей на этапе линковки
- **Создан для 32-битных МК** — целочисленный конвейер, без участия FPU, скорость близка к пределу
- **Нулевое выделение на горячем пути** — нет `malloc`/`new`, нет выделения блоков кучи во время разбора; потребление памяти ограничено на этапе компиляции
- **Ноль копирований** — токенизация через `std::string_view`, никаких промежуточных строк-подстрок
- **Целочисленный** — координаты хранятся как масштабированные целые (1e-7 градуса), FPU не требуется
- **Быстрый** — 11+ миллионов предложений/сек на x86_64; на идентичных данных измерено **в 3.2 раза быстрее** minmea и **~в 70 раз быстрее** TinyGPS++ (см. [Сравнение](#сравнение-с-другими-nmea-0183-парсерами))
- **Компактный** — ~1.3 КБ Flash, 52 байта RAM на Cortex-M0+
- **Поддерживаемые сообщения** — RMC, GGA, VTG, GLL
- **Учёт созвездий** — Talker ID -> GPS / GLONASS / Galileo / BeiDou / QZSS / NavIC / Multi с фильтрацией битовыми масками (`CST_MASK_*`)
- **Несколько форматов вывода** — TXT, CSV, JSON, Compact
- **Конвертация единиц** — скорость (км/ч, узлы, м/с), расстояние (м, футы, км)
- **Потоковый режим** — `NMEAStreamReader` с антиспам-фильтром и статистикой
- **Таблицы на этапе компиляции** — `constexpr`-таблицы для маршрутизации Talker ID и проверки CRC
- **Современный C++ (совместим с C++17)** — `constexpr`-таблицы, подсказки ветвлений `[[likely]]`, строгие enum-типы, не требует RTTI/exceptions

## Требования

| Компилятор | Минимальная версия |
|------------|--------------------|
| GCC        | >= 9 (C++17 или C++20) |
| Clang      | >= 12 (C++17 или C++20) |
| MSVC       | VS 2019 16.10+          |

- Стандарт: **C++17** (базовый — каждый скрипт сборки в этом репозитории использует `-std=c++17`); те же исходники чисто собираются и как C++20. Сборка `-std=c++17` проходит без предупреждений (проверено с `-Wall -Wextra -Wpedantic` на GCC 14) без измеримой разницы в производительности.
- Рекомендуемые флаги для МК: `-fno-exceptions -fno-rtti -Os` (полностью поддерживаются)
- Платформы: 32-битные МК (Cortex-M, ESP32, ESP8266, STM32, RP2040), Arduino (AVR, SAMD) и любой POSIX-десктоп

> **Примечание для пользователей 8-битных МК (AVR, PIC)**
>
> В light-nmea-cpp нет ничего, что требовало бы 32-битного железа: конвейер разбора — чистая целочисленная математика — ни FPU, ни кучи, ни деления на горячем пути, — так что библиотека естественно подходит и для 8-битных микроконтроллеров. Единственное, что стоит между вами и библиотекой, — компилятор: ему нужны возможности C++17 вроде `std::string_view` (подсказки `[[likely]]`/`[[unlikely]]` — из C++20, но GCC >= 9 и Clang >= 12 принимают их и в режиме C++17 как безвредные расширения).
>
> Если ваш 8-битный тулчейн всё ещё ниже C++17, достаточно обновить компилятор: свежие релизы avr-gcc уже поддерживают C++17, а в ядре нет ни одного 32-бит-специфичного кода — парсеру просто нужен компилятор с поддержкой C++17.

## Какой стандарт C++ используется?

Библиотека написана на **C++17**, и каждый скрипт сборки в этом репозитории
использует `-std=c++17` (bash-скрипты, PlatformIO, бенчмарки, модульные
тесты). Единственное место, где по умолчанию стоит C++20, — это
[`CMakeLists.txt`](lib/LightNMEA/CMakeLists.txt) библиотеки
(`CMAKE_CXX_STANDARD 20`): переключите его на 17 — те же исходники соберутся
без предупреждений и там.

Почему базовый стандарт — C++17?

- **`[[likely]]` / `[[unlikely]]`** формально являются атрибутами C++20, но
  GCC >= 9 и Clang >= 12 принимают их в режиме C++17 как безвредные
  расширения — подсказки предсказания ветвлений на горячем пути парсера
  работают без требования C++20.
- **Нет конструкций, доступных только с C++20** (`std::span`,
  `std::to_underlying`, ...): библиотека поставляет собственную минимальную
  `to_underlying()` и чисто компилируется как `-std=c++17 -Wall -Wextra
  -Wpedantic` (проверено на GCC 14, ноль предупреждений).
- **Нет измеримой разницы**: бенчмарк на x86-64 показывает одинаковую
  скорость для сборок C++17 и C++20 в пределах погрешности измерений (+/-5%).

Почему не C++23?

- Он был просто слишком молод: на момент разработки ни один крупный
  MCU-тулчейн — arm-none-eabi-gcc, Arduino-ядра ESP32/STM32/RP2040, avr-gcc —
  не имел полной поддержки C++23.
- Возможности, которые помогли бы (`std::to_underlying`, `std::expected`), —
  удобства, а не необходимость.

## Установка

### Arduino IDE

1. Скачайте репозиторий как **ZIP** (Code -> Download ZIP)
2. В Arduino IDE: **Sketch -> Include Library -> Add .ZIP Library**
3. Выберите скачанный ZIP
4. Используйте `#include <lightnmea.h>`

### PlatformIO

Добавьте в ваш `platformio.ini`:

```ini
lib_deps = octaprog7/light-nmea-cpp
```

### CMake

```cmake
add_subdirectory(lib/LightNMEA)          # interface target: light_nmea
target_link_libraries(your_app PRIVATE light_nmea)
```

или через `FetchContent`:

```cmake
FetchContent_Declare(light_nmea
    GIT_REPOSITORY https://github.com/octaprog7/light-nmea-cpp.git
    GIT_TAG        v1.0.0)
FetchContent_MakeAvailable(light_nmea)
```

## Быстрый старт

### POSIX / десктоп

```cpp
#include <lightnmea.h>
#include <cstdio>
#include <string_view>

int main() {
    light_nmea::Parser parser;

    std::string_view line =
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

    if (parser.parse_line(line)) {
        const auto& data = parser.data();
        if (data.has_coordinates()) {
            std::printf("Lat: %.6f  Lon: %.6f\n",
                        data.latitude_deg(), data.longitude_deg());
        }
    }
}
```

### Arduino / 32-битный МК

```cpp
#include <lightnmea.h>

light_nmea::Parser parser;
char buf[120];
size_t len = 0;

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);   // GPS-модуль
}

void loop() {
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            buf[len] = '\0';
            if (parser.parse_line(buf)) {
                const auto& d = parser.data();
                if (d.has_coordinates()) {
                    Serial.print("Lat: ");
                    Serial.println(d.latitude_deg(), 6);
                    Serial.print("Lon: ");
                    Serial.println(d.longitude_deg(), 6);
                }
            }
            len = 0;
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        }
    }
}
```

## Справочник по API

### `light_nmea::Parser`

Главная точка входа. Не сохраняет состояние между предложениями, рассчитан на переиспользование.

| Член                                     | Описание                                                                    |
|------------------------------------------|-----------------------------------------------------------------------------|
| `bool parse_line(std::string_view line)` | Разобрать одно NMEA-предложение (с `$`, контрольной суммой `*XX`). Возвращает `true` при успехе |
| `const NMEAData& data() const`           | Доступ к данным последнего успешно разобранного предложения                 |
| `struct Config`                          | Настройки, например `trust_gga_fix`                                         |
| `struct Diagnostics`                     | Счётчики, например `reject_crc`                                             |

### `NMEAData` и типобезопасные типы

Разобранные поля хранятся в строгих целочисленных типах из `types.hpp`:
`Latitude`, `Longitude`, `Heading`, `Speed` (сырое значение `int32_t`, масштаб 1e-7 для координат).

Ключевые аксессоры:

| Аксессор                                       | Возвращает          |
|------------------------------------------------|---------------------|
| `latitude_deg()` / `longitude_deg()`           | double, градусы     |
| `speed_kmh()` / `speed_knots()` / `speed_ms()` | конвертированная скорость |
| `course_true_deg()`                            | курс в градусах     |
| `altitude_m()`                                 | высота в метрах     |
| `hdop_value()`                                 | значение HDOP       |
| `has_coordinates()`                            | валидность широты/долготы |

Остальные поля: `time_utc`, `date`, `satellites`, `valid`, `constellation` (`Constellation`), `fix_mode` (`FixMode`).

### Константы и фильтрация

| Константа                        | Значение                                      |
|----------------------------------|-----------------------------------------------|
| `MAX_PACKET_SIZE` (120)          | максимальная принимаемая длина предложения    |
| `MAX_COMMAS` (14)                | максимальное число полей                      |
| `CST_MASK_GPS ... CST_MASK_ALL`  | битовые маски созвездий для `set_cst_filter()` |

`MessageType`: `RMC`, `GGA`, `VTG`, `GLL`.
`FixMode`: `Autonomous`, `DGPS`, `Estimated`, `NotValid`, `RTKFixed`, `RTKFloat`, `PPS`, `ManualInput`, `Simulator`, `Precise`.

## Форматтеры

`nmea_formatter.hpp` выводит разобранные данные без выделений (стековый `BufferWriter`):

```cpp
Format::TXT | Format::CSV | Format::JSON | Format::COMPACT
```

Единицы измерения: `SpeedUnit { KMH, KNOTS, MS }`, `DistanceUnit { METERS, FEET, KILOMETERS }`.
Форматирование через `data.to_txt()`, `data.to_csv()`, `data.to_json()`, `data.to_compact()` или единый `data.to_format(format)`.

## Потоковый режим

Для непрерывных источников данных (UART, TCP, файлы):

- **`NMEAStreamReader`** — автоматическая нарезка предложений + антиспам-фильтр (GGA/RMC), собирает `Stats { packets_processed, ... }`
- **`Stream`** — абстрактный интерфейс `read()` для любого источника байтов
- **`SerialStream`** — POSIX-последовательный порт (`/dev/ttyACM*`, `/dev/ttyUSB*`)
- **`FileStream`** — воспроизведение записанных треков `*.nmea`

Пример: `examples/tcp_stream_example.cpp` + `scripts/socat_tcp_demo.py` демонстрируют полную цепочку `socat (PTY->TCP) -> TCP-клиент -> Parser`.

## Примеры

| Пример                                                                         | Платформа | Описание                                                       |
|---------------------------------------------------------------------------------|-----------|----------------------------------------------------------------|
| [`BasicGPS.ino`](lib/LightNMEA/examples/BasicGPS/BasicGPS.ino)                  | Arduino   | Минимальный GPS-дисплей                                        |
| [`GPSToJSON.ino`](lib/LightNMEA/examples/GPSToJSON/GPSToJSON.ino)               | Arduino   | Разбор и вывод JSON                                            |
| [`GPSLogger.ino`](lib/LightNMEA/examples/GPSLogger/GPSLogger.ino)               | Arduino   | Логирование GPS-данных                                         |
| [`stream_reader_example.cpp`](lib/LightNMEA/examples/stream_reader_example.cpp) | POSIX     | Поток как UART + воспроизведение файла                         |
| [`serial_reader_example.cpp`](lib/LightNMEA/examples/serial_reader_example.cpp) | POSIX     | Чтение последовательного порта                                 |
| [`tcp_stream_example.cpp`](lib/LightNMEA/examples/tcp_stream_example.cpp)       | POSIX     | TCP-поток (см. socat-демо)                                     |
| [`gps_daemon.cpp`](lib/LightNMEA/examples/gps_daemon.cpp)                       | Linux     | GPS-демон: serial -> CSV-файл, демонизация (`-d`), автопереподключение |

Пример [**gps_daemon**](lib/LightNMEA/examples/gps_daemon.cpp) показывает полный Linux-конвейер -- `SerialStream -> NMEAStreamReader -> Parser -> to_csv()`: он читает реальное последовательное устройство (`/dev/ttyACM*`, `/dev/ttyUSB*`), пишет одну CSV-строку на каждый валидный фикс, переживает отключения устройства, реагирует на SIGINT/SIGTERM и может работать в фоне:

```bash
# на переднем плане: устройство, скорость, выходной файл
./gps_daemon /dev/ttyACM0 9600 /tmp/gps.csv

# фоновый демон
./gps_daemon -d /dev/ttyUSB0 38400 /var/log/gps.csv
```

Чтобы протестировать без железа, подайте записанный NMEA через виртуальный последовательный порт:

```bash
socat -d -d PTY,link=/tmp/gps_pty,raw,echo=0 \
      SYSTEM:"cat assets/nmea/trace_000.nmea; sleep 2" &
./gps_daemon -s 1 /tmp/gps_pty 9600 /tmp/gps_out.csv
```

Готовая к прошивке демо-прошивка (RP2040 Pico GPS-логгер, CSV по USB) живёт в [`src/main.cpp`](src/main.cpp) — сборка через PlatformIO: `pio run -t upload` (см. [`build_pico.sh`](build_pico.sh)).

## Производительность

Измерено на реальном железе с помощью включённых бенчмарков (`bench_parser`):

| Платформа              | Скорость                          | Flash   | RAM  |
|------------------------|-----------------------------------|---------|------|
| x86_64 (Ryzen 7 2700)  | 11-14 M пакетов/с                 | --      | --   |
| Cortex-M0+             | в 5-10 раз быстрее float-парсеров | ~1.3 КБ | 52 Б |
| ESP32                  | 11+ M пакетов/с                   | ~1.3 КБ | 52 Б |

Сравнительные on-target цифры для **RP2040 / ESP32** (LightNMEA против
[minmea](https://github.com/kosma/minmea) и
[TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) на одной плате) даёт
прошивочный бенчмарк:
[`build_bench_fw.sh`](lib/LightNMEA/benchmarks/build_bench_fw.sh) ->
[`benchmarks/firmware/`](lib/LightNMEA/benchmarks/firmware/).

### Сравнительный бенчмарк на плате: сборка и прошивка по шагам

Прошивка ([`bench_compare_mcu.cpp`](lib/LightNMEA/benchmarks/firmware/src/bench_compare_mcu.cpp))
прогоняет одинаковые пакеты из [`nav_gen.hpp`](lib/LightNMEA/include/nav_gen.hpp)
(RMC/GGA/VTG/GLL) через все три парсера на самой плате и печатает pkt/s в
Serial @ 115200. Сборка — **Release** (`-O2`, релизный режим PlatformIO).

1. Установите [PlatformIO](https://platformio.org/) (CLI `pio`):

   ```bash
   pip install -U platformio
   ```

2. Подключите плату к хосту по USB.

3. Соберите прошивку (из `lib/LightNMEA/benchmarks/`, выбрав env под вашу
   плату):

   ```bash
   ./build_bench_fw.sh pico run        # Raspberry Pi Pico (RP2040)
   ./build_bench_fw.sh esp32 run       # обычная отладочная плата ESP32
   ./build_bench_fw.sh d1_mini32 run   # WEMOS D1 mini ESP32
   ./build_bench_fw.sh esp32s3 run     # ESP32-S3-DevKitC-1
   ./build_bench_fw.sh esp32c3 run     # ESP32-C3-DevKitM-1 (RISC-V)
   ./build_bench_fw.sh esp8266 run     # WEMOS/NodeMCU D1 mini ESP8266
   ./build_bench_fw.sh bluepill run    # STM32 Blue Pill (F103C8)       [компиляция не проверена]
   ./build_bench_fw.sh teensy41 run    # Teensy 4.1                     [компиляция не проверена]
   ./build_bench_fw.sh feather_m0 run  # Adafruit Feather M0 (SAMD21)   [компиляция не проверена]
   ```

   (компиляция проверена: pico, esp32, d1_mini32, esp32s3, esp32c3, esp8266;
   env `bluepill`, `teensy41` и `feather_m0` настроены, но пока не проверены —
   при первой сборке PlatformIO сам скачает нужные пакеты)

4. Прошейте плату (порт определяется автоматически):

   ```bash
   ./build_bench_fw.sh pico upload     # либо имя любого другого env
   ```

   > Для Pico: если на плате запущена другая прошивка (например, MicroPython),
   > она может не перезагрузиться в BOOTSEL автоматически, и `upload` упадёт с
   > «No new RPxxxx device found». Зажмите **BOOTSEL** при подключении платы
   > либо войдите в BOOTSEL из работающей прошивки (MicroPython:
   > `machine.bootloader()`), затем повторите загрузку.

5. Считайте результаты через Serial @ 115200:

   ```bash
   ./build_bench_fw.sh d1_mini32 monitor
   ```

   (или любым серийным терминалом на 115200; учтите, что `pio device monitor`
   требует интерактивного терминала на stdin — для скриптов читайте порт
   напрямую, например через pyserial). Бенчмарк запускается заново при каждой
   перезагрузке платы.

Измеренные результаты (30 000 пакетов × 3 прохода, приведён лучший проход):

**Raspberry Pi Pico (RP2040 @ 133 МГц, Cortex-M0+, без FPU, arm-none-eabi-gcc 14.2.0, `-O2`)**:

| Реализация  | pkt/s      | ns/pkt  | cycles/pkt | pkt/s/МГц | ok/total    |
|-------------|------------|---------|------------|-----------|-------------|
| **LightNMEA**  | **47 579** | **21 017** | **2 795** | **357** | 30000/30000 |
| minmea         | 20 121     | 49 698  | 6 610      | 151       | 30000/30000 |
| TinyGPSPlus    | 17 430     | 57 369  | 7 630      | 131       | 30000/30000 |

На одинаковых пакетах на плате LightNMEA **в ~2.4 раза быстрее** minmea и
**в ~2.7 раза быстрее** TinyGPSPlus, у всех трёх — 100% успешных разборов.

**WEMOS D1 mini ESP32 (Xtensa LX6 @ 240 МГц, xtensa-esp32-gcc 8.4.0, `-O2`)**:

| Реализация  | pkt/s       | ns/pkt  | cycles/pkt | pkt/s/МГц | ok/total    |
|-------------|-------------|---------|------------|-----------|-------------|
| **LightNMEA**  | **110 629** | **9 039**  | **2 169** | **460** | 30000/30000 |
| minmea         | 33 955      | 29 450  | 7 068      | 141       | 30000/30000 |
| TinyGPSPlus    | 33 882      | 29 514  | 7 083      | 141       | 30000/30000 |

На одинаковых пакетах на плате LightNMEA **в ~3.3 раза быстрее** обоих
конкурентов, у всех трёх — 100% успешных разборов.

### Сводка по платформам (десктоп + ESP32 + RP2040)

| Платформа                                            | Вход                         | LightNMEA      | minmea  | TinyGPS++ | vs minmea | vs TinyGPS++ |
|------------------------------------------------------|------------------------------|----------------|---------|-----------|-----------|--------------|
| Десктоп x86_64 (GCC 14.2, `-O3`)                     | `trace_000.nmea` (10k OSM)   | 6.63 M строк/с | 1.92 M  | 80.2 k    | **3.4x**  | **82.8x**    |
| WEMOS D1 mini ESP32 (Xtensa LX6 @ 240 МГц, `-O2`)    | `nav_gen` (RMC/GGA/VTG/GLL)  | 110 629 pkt/s  | 33 955  | 33 882    | **3.3x**  | **3.3x**     |
| Raspberry Pi Pico (RP2040 @ 133 МГц, без FPU, `-O2`) | `nav_gen` (RMC/GGA/VTG/GLL)  | 47 579 pkt/s   | 20 121  | 17 430    | **2.4x**  | **2.7x**     |

Все парсеры показывают 100% успешных разборов на чистых данных; на МК преимущество стабильно независимо от тактовой частоты — LightNMEA тратит 2 169 циклов/пакет (ESP32) и 2 795 (RP2040) против 7 068/6 610 у minmea и 7 083/7 630 у TinyGPS++. Абсолютные pkt/s на десктопе и на МК напрямую не сравнимы (разные датасеты и флаги) — сравнивайте коэффициенты, а не абсолютные значения. На десктопе `bench_compare` дополнительно покрывает [libnmea](https://github.com/jacketizer/libnmea) через `bench_parser` (`-i ALL`, см. ниже).

- Удельная эффективность: ~1,950-4,375 предложений/МГц
- На 32-битных МК целочисленный конвейер даёт скорость, близкую к теоретическому пределу, — ни FPU, ни деления, ни обращения к куче на горячем пути
- Сравнительные числа против лидеров экосистемы — [minmea](https://github.com/kosma/minmea), [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus) — измеренные на идентичных данных, приведены в следующем разделе

Запуск локально:

```bash
./bench.sh quick     # самый простой: автосборка + скорость разбора только LightNMEA (100k)
./bench.sh compare   # все 4 реализации, 0% и 10% повреждённых пакетов
./bench.sh types     # сравнение по типам предложений (RMC / GGA / VTG / GLL)
./bench.sh navgen    # скорость генерации навигационных пакетов
# или по шагам:
cd lib/LightNMEA/benchmarks
./build.sh           # собирает bench_parser, bench_nav_gen
./build/bench_parser
```

## Сравнение с другими NMEA-0183 парсерами

Сравнение один на один против двух самых показательных парсеров в экосистеме NMEA-0183:

- **[minmea](https://github.com/kosma/minmea)** — широко используемый эталонный парсер на чистом C, известный как быстрая и лёгкая реализация
- **[TinyGPS++](https://github.com/mikalhart/TinyGPSPlus)** — самая популярная Arduino-библиотека для GPS

Методология (реализована в [`bench_compare.cpp`](lib/LightNMEA/benchmarks/compare/bench_compare.cpp), полная процедура ниже в разделе [Бенчмарки и данные](#бенчмарки-и-данные)): идентичный вход — 10 000 реальных OSM GPS-предложений из релизного датасета; идентичные компилятор и флаги (GCC 14, `-O3`); лучший из 5 прогонов; каждый парсер показал 100% успешных разборов.

| Парсер             | Строк/с      | Относительно light-nmea-cpp |
|--------------------|--------------|------------------------------|
| **light-nmea-cpp** | **~5.68 M**  | **1.00x**                    |
| minmea (C)         | ~1.79 M      | 0.32x                        |
| TinyGPS++ (C++)    | ~0.08 M      | 0.014x                       |

Оба конкурента хранят координаты в плавающей точке, а TinyGPS++ вдобавок потребляет вход посимвольно через свой конечный автомат `encode()`, что и доминирует в его времени работы. light-nmea-cpp держит всё целочисленным и нарезает поля через `std::string_view` за один проход — именно поэтому она лидирует на одинаковых данных, компиляторе и флагах.

Бенчмарк [`bench_parser.cpp`](lib/LightNMEA/benchmarks/bench_parser.cpp) на основе плана индексов (`-i ALL`, 1 000 000 пакетов, 5% повреждённых: битый CRC + обрыв) дополнительно покрывает [libnmea](https://github.com/jacketizer/libnmea) на идентичном входе:

| Парсер             | пакетов/с   | vs light-nmea-cpp |
|--------------------|-------------|-------------------|
| **light-nmea-cpp** | **~8.6 M**  | **1.00x**         |
| minmea (C)         | ~2.2 M      | 0.25x             |
| libnmea (C)        | ~1.8 M      | 0.20x             |
| TinyGPS++ (C++)    | ~0.12 M     | 0.014x            |

Запустите сами: `cd lib/LightNMEA/benchmarks && ./run_bench_parser.sh ALL 5 1000 RND ALL`.

## Тесты

Модульные тесты на CTest в `lib/LightNMEA/tests`:

```bash
cd lib/LightNMEA
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Покрытие: парсер (`test_parser`), потоковый ридер (`test_stream_reader`), форматтеры (`test_formatter`).

### Проверка качества (`scripts/run_quality_checks.sh`)

Однокомандная проверка качества исполнения — чистая компиляция, модульные тесты, фаззинг, санитайзеры, сборка без исключений:

```bash
cd lib/LightNMEA
./scripts/run_quality_checks.sh             # все проверки
./scripts/run_quality_checks.sh sanitize    # ASan + UBSan + обнаружение утечек
./scripts/run_quality_checks.sh fuzz 1000000
```

Текущий статус (GCC 14.2, x86-64):

| Проверка                                                                                                                            | Результат                |
|-------------------------------------------------------------------------------------------------------------------------------------|--------------------------|
| Чистая компиляция C++17 (`-Wall -Wextra -Wpedantic`)                                                                                | [OK] 0 предупреждений    |
| Модульные тесты (`test_parser`, `test_stream_reader`, `test_formatter`)                                                             | [OK] все проходят        |
| Фаззинг — детерминированный (каждый байт каждого пакета: инверсия/обрыв/стирание/вставка) + 1 000 000 случайных мутаций, восстановление после мусора | [OK] OK          |
| ASan + UBSan + проверка утечек (модульные тесты + 200 000 фаззинг-мутаций)                                                          | [OK] чисто               |
| Сборка без исключений / без RTTI (`-fno-exceptions -fno-rtti`)                                                                      | [OK] OK                  |
| valgrind memcheck                                                                                                                   | пропущено (не установлен)|

Фаззинг-тест [`tests/fuzz_parser.cpp`](lib/LightNMEA/tests/fuzz_parser.cpp) проверяет инварианты координат на каждом принятом пакете и убеждается, что парсер восстанавливается после мусорного потока.

### Отчёт о занимаемом коде (`code_report.sh`)

Доказательство — по скомпилированному Release-коду — что ядро парсера свободно от FPU, зависимостей и выделений, плюс точный размер кода в байтах и плотность инструкций. Все числа берутся стандартными GNU binutils (`size`, `nm`, `objdump`, `readelf`). Анализ реализован на Python ([`code_report.py`](lib/LightNMEA/scripts/code_report.py)); `code_report.sh` — тонкая обёртка для удобства:

```bash
./code_report.sh                # авто: Cortex-M0+ если есть arm-none-eabi-g++, иначе native
./code_report.sh --m0           # сборка probe для Cortex-M0+ (soft-float, без FPU)
./code_report.sh --native       # сборка probe для хоста (x86-64)
./code_report.sh --quick (-q)   # краткий вывод: размер / FPU / зависимости / куча / плотность
./code_report.sh <file.o|elf>   # анализ готового объекта / ELF прошивки
```

Разделы отчёта: (1) `.text`/`.data`/`.bss`/`.rodata` и итоги Flash/RAM, (2) FPU-инструкции + вызовы soft-float, (3) динамические `NEEDED` + внешние символы, (4) вызовы аллокатора кучи (`malloc`/`new`/`sbrk`/`mmap` ...), (5) плотность кода — число инструкций, средняя ширина инструкции (байты) и инструкций на КиБ. Probe ([`code_report_probe.cpp`](lib/LightNMEA/scripts/code_report_probe.cpp)) использует только целочисленное ядро (`parse_line` + сырые значения); опциональные float-геттеры и `nmea_formatter.hpp` намеренно исключены.

Пример (`--m0`, GCC 13 arm-none-eabi): код = 5011 Б, Flash ~ 4.93 КиБ, RAM = 0 Б; FPU-инструкций = 0; вызовов soft-float = 0; вызовов аллокатора = 0; средняя ширина инструкции = 2.03 Б (~ теоретический минимум Thumb), ~503 инструкций/КиБ. Код возврата: 0 = все проверки пройдены.

## Бенчмарки и данные

- `benchmarks/bench_parser.cpp` — настраиваемый бенчмарк скорости разбора против [minmea](https://github.com/kosma/minmea), [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus), [libnmea](https://github.com/jacketizer/libnmea) (`-i ALL`)
- `benchmarks/build_bench_parser.sh` / `benchmarks/run_bench_parser.sh` — прямая сборка gcc/g++ и запуск одной командой
- `benchmarks/run_benchmarks.sh` — типовые сценарии (quick / compare / types / navgen / file), также доступен из корня репозитория как [`bench.sh`](bench.sh)
- `benchmarks/bench_nav_gen.cpp` — скорость генерации навигационных пакетов
- `benchmarks/compare/bench_compare.cpp` — сравнение один на один с [minmea](https://github.com/kosma/minmea) и [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus) на идентичных реальных данных ([`build.sh`](lib/LightNMEA/benchmarks/compare/build.sh) оборачивает сборку)

### Методология `bench_compare`

[`bench_compare.cpp`](lib/LightNMEA/benchmarks/compare/bench_compare.cpp) измеряет три парсера «от начала до конца» на **одном и том же входе**:

- **Вход**: файл `.nmea`, одно предложение на строку (`bench_compare [file.nmea]`); пустые строки и хвостовые `\r` удаляются. Датасет, использованный для опубликованных чисел, — `trace_000.nmea` из релизных ассетов (10 000 реальных OSM GPS-предложений).
- **Честность**: все парсеры линкуются в один бинарник и компилируются одним компилятором с одними флагами (`g++ -std=c++17 -O3`), так что CPU, тулчейн и уровень оптимизации идентичны для каждого парсера.
- **Метрика**: предложений в секунду — сквозной разбор каждой строки — плюс доля успешно разобранных предложений (`ok/total`, %). На реальном датасете все три парсера показывают ~100%.
- **Лучший из 5**: каждый парсер замеряется 5 раз, сохраняется минимальное время; каждая итерация пересоздаёт объект парсера, поэтому нет смещения от «прогрева» между парсерами.
- **Без удаления мёртвого кода**: разобранные значения накапливаются в глобальный `volatile int64_t g_sink`, чтобы компилятор не смог выбросить работу по разбору.
- **Парсер-специфичные точки входа** (каждый парсер использует свой естественный API):
  - LightNMEA — `Parser::parse_line()` для каждой строки;
  - minmea — диспетчеризация `minmea_sentence_id()` на `minmea_parse_rmc/gga/vtg/gll`;
  - TinyGPS++ — посимвольный `encode(c)` с явным завершителем `'\n'` (зеркалит реальную нарезку потока); успех считается через `passedChecksum()`.
- **Вывод**: настенное время каждого парсера, строк/с, `ok/total` (%) и отношение к light-nmea-cpp.

Воспроизведите сравнение сами (подправьте путь к датасету под вашу копию релизных ассетов):

```bash
cd lib/LightNMEA/benchmarks/compare
./build.sh run ../../../../assets/nmea/trace_000.nmea
```

Реальные треки OpenStreetMap (NMEA + исходные GPX) прикреплены к **GitHub Releases как ассеты** (`assets/nmea/`), а не хранятся в репозитории. Чтобы перегенерировать их:

```bash
python3 scripts/download_osm_traces.py   # скачать сырые GPX-треки
python3 scripts/gpx_to_nmea_v2.py        # конвертировать GPX -> NMEA
```

## Структура репозитория

```
light-nmea-cpp/
├── bench.sh                            # Запуск бенчмарков одной командой (quick / compare / types / navgen / file)
├── build_pico.sh                       # Сборка/заливка демо-прошивки RP2040 через PlatformIO
├── code_report.sh                      # Отчёт о занимаемом коде одной командой (FPU/зависимости/куча/размер/плотность)
├── platformio.ini                      # PlatformIO-проект (демо-прошивка + встроенная библиотека)
├── src/
│   ├── main.cpp                        # Демо-прошивка RP2040 Pico: GPS-логгер, CSV по USB
│   └── README.md
└── lib/LightNMEA/                      # Сама библиотека (только заголовки; Arduino / PlatformIO / CMake)
    ├── CMakeLists.txt                  # INTERFACE target `light_nmea`, модульные тесты, примеры
    ├── CMakePresets.json               # CMake-пресеты (configure/build/test)
    ├── library.json                    # PlatformIO library manifest
    ├── library.properties              # метаданные Arduino Library Manager
    ├── keywords.txt                    # подсветка синтаксиса Arduino IDE
    ├── LICENSE                         # GPL-3.0
    ├── include/
    │   ├── lightnmea.h                 # Arduino-дружелюбная точка входа (один include)
    │   ├── light_nmea.hpp              # Ядро: Parser, NMEAData, constexpr-таблицы, CRC, fixed-point разбор
    │   ├── types.hpp                   # Строгие целочисленные типы: Latitude, Longitude, Heading, Speed (+ конверсия без FPU)
    │   ├── utils.hpp                   # Общие вспомогательные функции
    │   ├── nav_gen.hpp                 # Детерминированный генератор NMEA-предложений (тесты / бенчмарки)
    │   ├── nmea_formatter.hpp          # Форматтеры без выделений: TXT / CSV / JSON / Compact
    │   ├── nmea_stream_reader.hpp      # Потоковый ридер: нарезка, антиспам-фильтр, статистика
    │   ├── stream.hpp                  # Абстрактный интерфейс источника байтов (read())
    │   └── serial_stream.hpp           # Реализация POSIX-порта (UART)
    ├── examples/
    │   ├── BasicGPS/                   # Arduino: минимальный GPS-дисплей
    │   ├── GPSLogger/                  # Arduino: логирование GPS на SD-карту
    │   ├── GPSToJSON/                  # Arduino: разбор и вывод JSON
    │   ├── stream_reader_example.cpp   # POSIX: поток как UART + воспроизведение файла
    │   ├── serial_reader_example.cpp   # POSIX: прямое чтение порта (/dev/ttyXXX)
    │   ├── tcp_stream_example.cpp      # POSIX: NMEA по TCP (см. socat-демо)
    │   └── gps_daemon.cpp              # Linux: SerialStream -> Parser -> CSV демон (-d, автопереподключение)
    ├── tests/
    │   ├── test_parser.cpp             # Модульные тесты: поведение парсера и регрессии (вкл. поля без десятичной точки)
    │   ├── test_stream_reader.cpp      # Модульные тесты: нарезка потока / антиспам / восстановление
    │   ├── test_formatter.cpp          # Модульные тесты: форматтеры TXT / CSV / JSON / Compact
    │   └── fuzz_parser.cpp             # Фаззинг: детерминированный + 1M случайных мутаций, инварианты координат
    ├── scripts/
    │   ├── run_quality_checks.sh       # Полный контроль качества: чистая сборка, юнит-тесты, фаззинг, ASan/UBSan, без исключений
    │   ├── code_report.py              # Отчёт о footprint кода: размер, FPU, зависимости, куча, плотность (binutils)
    │   ├── code_report_probe.cpp       # Probe для footprint-отчёта (только целочисленное ядро парсера)
    │   ├── download_osm_traces.py      # Скачивание реальных треков OpenStreetMap для бенчмарк-датасетов
    │   ├── gpx_to_nmea_v2.py           # Конвертация GPX -> NMEA-предложения
    │   └── socat_tcp_demo.py           # Демо: виртуальный порт -> TCP (питает tcp_stream_example)
    └── benchmarks/
        ├── bench_parser.cpp            # Настраиваемый бенчмарк скорости разбора (LIGHT / MINMEA / TINYGPS / LIBNMEA)
        ├── bench_nav_gen.cpp           # Скорость генерации nav_gen
        ├── build_bench_parser.sh       # Прямая сборка bench_parser gcc/g++ (без CMake)
        ├── build_bench_fw.sh           # Сборщик on-target firmware-бенчмарка (RP2040 / ESP32, PlatformIO)
        ├── run_bench_parser.sh         # Запуск bench_parser одной командой (автосборка)
        ├── run_benchmarks.sh           # Типовые сценарии: quick / compare / types / navgen / file
        ├── build.sh                    # CMake-сборка обоих бенчмарков
        ├── CMakeLists.txt              # Автономный бенчмарк-проект (C + C++)
        ├── compare/
        │   ├── bench_compare.cpp       # Сравнение один на один: LightNMEA vs minmea vs TinyGPSPlus на реальном .nmea-файле
        │   ├── build.sh                # Сборка / запуск / очистка bench_compare
        │   └── third_party/            # Вендорные парсеры для сравнения (minmea, TinyGPS++, libnmea)
        └── firmware/                   # PlatformIO-проект: бенчмарк на плате (LightNMEA vs minmea vs TinyGPSPlus)
            ├── platformio.ini          #   env:pico (RP2040), esp32, d1_mini32, esp32s3,
            │                           #   esp32c3, esp8266, bluepill (F103C8),
            │                           #   teensy41, feather_m0 (SAMD21)
            ├── src/bench_compare_mcu.cpp
            └── lib/                    #   вендорные копии minmea + TinyGPSPlus (раскладка src/include)
```

## Лицензия

**GPL-3.0** — см. [LICENSE](lib/LightNMEA/LICENSE).

Сторонний код, включённый для сравнительных бенчмарков (`benchmarks/compare/third_party/`), сохраняет собственные лицензии:
[minmea](lib/LightNMEA/benchmarks/compare/third_party/minmea.c) (WTFPL), [TinyGPS++](lib/LightNMEA/benchmarks/compare/third_party/TinyGPSPlus/TinyGPS++.h) (LGPL-2.1).

## Авторы

Roman Shevchik — автор [light-nmea-micropython](https://github.com/octaprog7/light-nmea-micropython) и этого C++-порта.