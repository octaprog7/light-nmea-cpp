# light-nmea-cpp

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Language](https://img.shields.io/badge/C%2B%2B-17-yellowgreen)
![Version](https://img.shields.io/badge/version-1.0.0-orange)
![Platform](https://img.shields.io/badge/platform-embedded%20%7C%20desktop-lightgrey)

**English** | [**Русский (README_RU.md)**](README_RU.md)

An ultra-fast NMEA 0183 GPS parser with **zero memory allocation**, written in modern C++ (**baseline standard — C++17; the same sources also build as C++20**).

The library targets **32-bit microcontrollers** (Cortex-M, ESP32, STM32, RP2040): an integer pipeline that **never touches the FPU**, and **no heap allocations on the hot path** — parsing speed is as close to the physical limit of the hardware as possible. At the same time, it works in high-frequency telemetry pipelines on the desktop — and is ready for 8-bit AVR boards as soon as your toolchain reaches the required C++ standard (see the note for 8-bit platform users below).

## Motivation

After releasing **[light-nmea-micropython](https://github.com/octaprog7/light-nmea-micropython)**, we found that the C++ niche remained practically open. Most NMEA parsers are written for Python or target the old Arduino-era C, leaving modern C++ developers without a truly fast, zero-allocation option.

The goal was set for **32-bit microcontrollers**: to achieve performance as close as possible to the practically achievable limit **without using the FPU** — pure integer arithmetic from sentence tokenization to coordinate math and formatting. And **no heap allocations on the hot path**: during parsing the library never touches the heap, so its memory footprint is strictly bounded at compile time and fits into any modern MCU.

light-nmea-cpp closes this gap. Its architecture and core parsing logic are directly inherited from **light-nmea-micropython** — proven in real-world projects — and rewritten in modern C++ (baseline standard — C++17, C++20-friendly). It brings the fastest NMEA parsing to those who write C++ professionally — and who would not trade their toolchain for a Python script. No runtime allocations, no floating point on the hot path — just parsing speed where it matters: on a 32-bit microcontroller or in a high-frequency telemetry pipeline.

## What you get

**Low MCU load.** At 5–14 million sentences/packets per second, parsing stops being a significant load. On a 32-bit MCU the parser runs tens and hundreds of times faster than the GPS module emits NMEA data packets.

**No memory allocation.** Zero heap allocation on the hot path means no `malloc` failures, no fragmentation, and no out-of-memory resets — even on devices that must run for months or years. Memory usage is fully known at compile time.

**Minimal program and data memory.** ~1.3 KB of Flash and 52 bytes of RAM let the parser run alongside a full application on a 'weak' MCU.

**Predictable behavior.** Bounded buffers, no recursion, no hidden state.

**Type safety.** Coordinates, speeds, and headings are strict types, not floats: mixing knots with km/h results in a compile error. Integer storage also means identical results across all platforms — no float rounding surprises.

**No configuration.** A single header file — plug and play: for Arduino, PlatformIO, CMake — whatever you use.

**Output 'out of the box'.** TXT, CSV, JSON, and Compact formatters are included; you won't have to invent output for logging or telemetry.

**From AVR to x86_64.** The same source runs on an 8-bit AVR, Cortex-M, ESP32, and a desktop server. Write once — run anywhere. Naturally, provided your toolchain supports C++17!

## Features

- **header-only** — no compilation or link-time dependencies
- **Built for 32-bit MCUs** — integer pipeline, no FPU/FPU emulation
- **No RAM allocation on the hot path** — no `malloc`/`new`, no heap allocations during parsing; memory usage is bounded at compile time
- **No copies** — tokenization via `std::string_view`, no intermediate strings/substrings
- **Integer-based** — coordinates are stored as scaled integers (1e-7 degrees), no FPU required
- **Fast** — 11+ million sentences/sec on x86_64; on identical data **3.2x faster** than minmea and **~70x faster** than TinyGPS++ (see [Comparison](#comparison-with-other-nmea-0183-parsers))
- **Small code size** — ~1.3 KB Flash, 52 bytes RAM on Cortex-M0+
- **Supported messages** — RMC, GGA, VTG, GLL
- **Constellation awareness** — Talker ID -> GPS / GLONASS / Galileo / BeiDou / QZSS / NavIC / Multi with bitmask filtering (`CST_MASK_*`)
- **Multiple output formats** — TXT, CSV, JSON, Compact
- **Unit conversion** — speed (km/h, knots, m/s), distance (m, feet, km)
- **Stream mode** — `NMEAStreamReader` with anti-spam filter and statistics
- **Compile-time tables** — `constexpr` tables for Talker ID routing and CRC checking
- **Modern C++ (C++17-compatible)** — `constexpr` tables, `[[likely]]` branch hints, strict enum types, no RTTI/exceptions required

## Software requirements

| Compiler | Minimum version        |
|----------|------------------------|
| GCC      | >= 9 (C++17 or C++20)  |
| Clang    | >= 12 (C++17 or C++20) |
| MSVC     | VS 2019 16.10+         |

- Standard: **C++17** (baseline — every build script in this repository uses `-std=c++17`); the sources also build as C++20. A `-std=c++17` build passes without warnings (verified with `-Wall -Wextra -Wpedantic` on GCC 14) with no measurable performance difference.
- Recommended flags for MCUs: `-fno-exceptions -fno-rtti -Os` (fully supported)
- Platforms: 32-bit MCUs (Cortex-M, ESP32, ESP8266, STM32, RP2040), Arduino (AVR, SAMD), and any POSIX desktop

> **Note for 8-bit MCU users (AVR, PIC)**
>
> There is nothing in light-nmea-cpp that requires 32-bit hardware: the parsing pipeline is based on integer math — no FPU, no heap, no division on the hot path — so the library naturally fits 8-bit microcontrollers as well. The only thing standing between you and the library is the compiler: it needs C++17 features such as `std::string_view` (`[[likely]]`/`[[unlikely]]` hints are from C++20, but GCC >= 9 and Clang >= 12 accept them in C++17 mode as extensions).
>
> If your 8-bit toolchain is still below C++17, simply update the compiler: recent avr-gcc releases may (or may not :-) support C++17, and there is not a single 32-bit-specific line of code in the core — the parser just needs a compiler with C++17 support.

## C++ standard

The library is written in **C++17**
and uses `-std=c++17` (bash scripts, PlatformIO, benchmarks, unit tests).
The only place where C++20 is the default is the library's
[`CMakeLists.txt`](lib/LightNMEA/CMakeLists.txt)
(`CMAKE_CXX_STANDARD 20`): switch it to 17 — the same sources will build
without warnings there too.

Why is the baseline standard C++17?

- **`[[likely]]` / `[[unlikely]]`** are C++20 attributes, but
  GCC >= 9 and Clang >= 12 accept them in C++17 mode as
  extensions — branch prediction hints on the parser's hot path
  work without C++20 compatibility.
- **No C++20-only constructs** (`std::span`,
  `std::to_underlying`, ...): the library provides its own minimal
  `to_underlying()` and compiles cleanly as `-std=c++17 -Wall -Wextra
  -Wpedantic` (verified on GCC 14, zero warnings).
- **No difference**: a benchmark on x86-64 shows the same
  speed for C++17 and C++20 builds within measurement error (+/-5%).

## Installation

### Arduino IDE

1. Download the repository as a **ZIP** (Code -> Download ZIP)
2. In the Arduino IDE: **Sketch -> Include Library -> Add .ZIP Library**
3. Select the downloaded ZIP
4. Use `#include <lightnmea.h>`

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps = https://github.com/octaprog7/light-nmea-cpp.git
```

> The library is not yet published to the PlatformIO Registry, so the
> git URL is used. Once published, you can use `octaprog7/light-nmea-cpp`.

### CMake

```cmake
add_subdirectory(lib/LightNMEA)          # interface target: light_nmea
target_link_libraries(your_app PRIVATE light_nmea)
```

or via `FetchContent`:

```cmake
FetchContent_Declare(light_nmea
    GIT_REPOSITORY https://github.com/octaprog7/light-nmea-cpp.git
    GIT_TAG        v1.0.0)
FetchContent_MakeAvailable(light_nmea)
```

## Quick start

### POSIX / desktop

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

### Arduino / 32-bit MCU

```cpp
#include <lightnmea.h>

light_nmea::Parser parser;
char buf[120];
size_t len = 0;

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);   // GPS module
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

## API reference

### `light_nmea::Parser`

The main entry point. Does not keep state between sentences, designed for reuse.

| Member                                   | Description                                                                                 |
|------------------------------------------|---------------------------------------------------------------------------------------------|
| `bool parse_line(std::string_view line)` | Parses a single NMEA sentence (with `$`, checksum `*XX`). Returns `true` on success         |
| `const NMEAData& data() const`           | Access to the data of the last successfully parsed sentence                                 |
| `struct Config`                          | Settings, e.g. `trust_gga_fix`                                                              |
| `struct Diagnostics`                     | Counters, e.g. `reject_crc`                                                                 |

### `NMEAData` and type-safe types

Parsed fields are stored in strict integer types from `types.hpp`:
`Latitude`, `Longitude`, `Heading`, `Speed` (raw `int32_t` value, 1e-7 scale for coordinates).

Main getter methods:

| Getter method                                  | Returns                             |
|------------------------------------------------|-------------------------------------|
| `latitude_deg()` / `longitude_deg()`           | double, degrees                     |
| `speed_kmh()` / `speed_knots()` / `speed_ms()` | converted speed                     |
| `course_true_deg()`                            | course in angular degrees           |
| `altitude_m()`                                 | altitude in meters                  |
| `hdop_value()`                                 | HDOP value                          |
| `has_coordinates()`                            | applicability of latitude/longitude |

Other fields: `time_utc`, `date`, `satellites`, `valid`, `constellation` (`Constellation`), `fix_mode` (`FixMode`).

### Constants and filtering

| Constant                        | Value                                         |
|---------------------------------|-----------------------------------------------|
| `MAX_PACKET_SIZE` (120)         | maximum accepted sentence length              |
| `MAX_COMMAS` (14)               | maximum number of fields in a sentence        |
| `CST_MASK_GPS ... CST_MASK_ALL` | constellation bitmasks for `set_cst_filter()` |

`MessageType`: `RMC`, `GGA`, `VTG`, `GLL`.
`FixMode`: `Autonomous`, `DGPS`, `Estimated`, `NotValid`, `RTKFixed`, `RTKFloat`, `PPS`, `ManualInput`, `Simulator`, `Precise`.

## Data formatting

`nmea_formatter.hpp` outputs parsed data without RAM allocation (stack-based `BufferWriter`):

```cpp
Format::TXT | Format::CSV | Format::JSON | Format::COMPACT
```

Units: `SpeedUnit { KMH, KNOTS, MS }`, `DistanceUnit { METERS, FEET, KILOMETERS }`.
Formatting via `data.to_txt()`, `data.to_csv()`, `data.to_json()`, `data.to_compact()` or the unified `data.to_format(format)`.

## Stream mode

For continuous data sources (UART, TCP, files):

- **`NMEAStreamReader`** — automatic sentence splitting + anti-spam filter (GGA/RMC), collects `Stats { packets_processed, ... }`
- **`Stream`** — abstract `read()` interface for any byte source
- **`SerialStream`** — POSIX serial port (`/dev/ttyACM*`, `/dev/ttyUSB*`)
- **`FileStream`** — replay of recorded `*.nmea` tracks

Example: `examples/tcp_stream_example.cpp` + `scripts/socat_tcp_demo.py` demonstrate the full chain `socat (PTY->TCP) -> TCP client -> Parser`.

## Examples

| Example                                                                         | Platform | Description                                                          |
|---------------------------------------------------------------------------------|----------|----------------------------------------------------------------------|
| [`BasicGPS.ino`](lib/LightNMEA/examples/BasicGPS/BasicGPS.ino)                  | Arduino  | Minimal GPS display                                                  |
| [`GPSToJSON.ino`](lib/LightNMEA/examples/GPSToJSON/GPSToJSON.ino)               | Arduino  | Parsing and JSON output                                              |
| [`GPSLogger.ino`](lib/LightNMEA/examples/GPSLogger/GPSLogger.ino)               | Arduino  | GPS data logging                                                     |
| [`stream_reader_example.cpp`](lib/LightNMEA/examples/stream_reader_example.cpp) | POSIX    | Stream from UART + file replay                                       |
| [`serial_reader_example.cpp`](lib/LightNMEA/examples/serial_reader_example.cpp) | POSIX    | Reading a serial port                                                |
| [`tcp_stream_example.cpp`](lib/LightNMEA/examples/tcp_stream_example.cpp)       | POSIX    | TCP stream (see socat demo)                                          |
| [`gps_daemon.cpp`](lib/LightNMEA/examples/gps_daemon.cpp)                       | Linux    | GPS daemon: serial -> CSV file, daemonization (`-d`), auto-reconnect |

The [**gps_daemon**](lib/LightNMEA/examples/gps_daemon.cpp) example is a complete Linux pipeline -- `SerialStream -> NMEAStreamReader -> Parser -> to_csv()`: it reads a real serial device (`/dev/ttyACM*`, `/dev/ttyUSB*`), writes one CSV line per valid fix, reacts to SIGINT/SIGTERM, and can run in the background:

```bash
# regular application: device, baud rate, output file
./gps_daemon /dev/ttyACM0 9600 /tmp/gps.csv

# background daemon
./gps_daemon -d /dev/ttyUSB0 38400 /var/log/gps.csv
```

To test without hardware, feed a stream of NMEA sentences through a virtual serial port:

```bash
socat -d -d PTY,link=/tmp/gps_pty,raw,echo=0 \
      SYSTEM:"cat assets/nmea/trace_000.nmea; sleep 2" &
./gps_daemon -s 1 /tmp/gps_pty 9600 /tmp/gps_out.csv
```

A flash-ready demo (RP2040 Pico GPS logger, CSV over USB) lives in [`src/main.cpp`](src/main.cpp) — build with PlatformIO: `pio run -t upload` (see [`build_pico.sh`](build_pico.sh)).

## Performance

Measured on real hardware with the benchmarks (`bench_parser`):

| Platform              | Speed                            | Flash   | RAM  |
|-----------------------|----------------------------------|---------|------|
| x86_64 (Ryzen 7 2700) | 11-14 M packets/s                | --      | --   |
| Cortex-M0+            | 5-10x faster than float parsers  | ~1.3 KB | 52 B |
| ESP32                 | 11+ M packets/s                  | ~1.3 KB | 52 B |

Comparative on-target numbers for **RP2040 / ESP32** (LightNMEA vs
[minmea](https://github.com/kosma/minmea) and
[TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) on the same board) are
provided by the benchmark firmware:
[`build_bench_fw.sh`](lib/LightNMEA/benchmarks/build_bench_fw.sh) ->
[`benchmarks/firmware/`](lib/LightNMEA/benchmarks/firmware/).

### On-board comparative benchmark: step-by-step build and flash

The firmware ([`bench_compare_mcu.cpp`](lib/LightNMEA/benchmarks/firmware/src/bench_compare_mcu.cpp))
runs identical packets from [`nav_gen.hpp`](lib/LightNMEA/include/nav_gen.hpp)
(RMC/GGA/VTG/GLL) through all three parsers on the board itself and prints pkt/s to
Serial @ 115200. The build is **Release** (`-O2`, PlatformIO release mode).

1. Install [PlatformIO](https://platformio.org/) (CLI `pio`):

   ```bash
   pip install -U platformio
   ```

2. Connect the board to the host over USB.

3. Build the firmware (from `lib/LightNMEA/benchmarks/`, choosing the env for your
   board):

   ```bash
   ./build_bench_fw.sh pico run        # Raspberry Pi Pico (RP2040)
   ./build_bench_fw.sh esp32 run       # generic ESP32 dev board
   ./build_bench_fw.sh d1_mini32 run   # WEMOS D1 mini ESP32
   ./build_bench_fw.sh esp32s3 run     # ESP32-S3-DevKitC-1
   ./build_bench_fw.sh esp32c3 run     # ESP32-C3-DevKitM-1 (RISC-V)
   ./build_bench_fw.sh esp8266 run     # WEMOS/NodeMCU D1 mini ESP8266
   ./build_bench_fw.sh bluepill run    # STM32 Blue Pill (F103C8)       [compilation not verified]
   ./build_bench_fw.sh teensy41 run    # Teensy 4.1                     [compilation not verified]
   ./build_bench_fw.sh feather_m0 run  # Adafruit Feather M0 (SAMD21)   [compilation not verified]
   ```

   (compilation verified: pico, esp32, d1_mini32, esp32s3, esp32c3, esp8266;
   the `bluepill`, `teensy41`, and `feather_m0` envs are configured but not verified —
   on the first build PlatformIO will download the required packages itself)

4. Flash the board (the port is detected automatically):

   ```bash
   ./build_bench_fw.sh pico upload     # or the name of any other env
   ```

   > For Pico: if another firmware is running on the board (e.g., MicroPython),
   > it may not reboot into BOOTSEL automatically, and `upload` will fail with
   > "No new RPxxxx device found". Hold **BOOTSEL** while connecting the board
   > or enter BOOTSEL from the running firmware (MicroPython:
   > `machine.bootloader()`), then retry the upload.

5. Read the results via Serial @ 115200:

   ```bash
   ./build_bench_fw.sh d1_mini32 monitor
   ```

   (or any serial terminal at 115200; note that `pio device monitor`
   requires an interactive terminal on stdin — for scripts, read the port
   directly, e.g. with pyserial). The benchmark re-runs on every
   board reset (press the RST button).

Measured results (30,000 packets × 3 passes, best-pass data shown):

**Raspberry Pi Pico (RP2040 @ 133 MHz, Cortex-M0+, no FPU, arm-none-eabi-gcc 14.2.0, `-O2`)**:

| Implementation | pkt/s      | ns/pkt     | cycles/pkt | pkt/s/MHz | ok/total    |
|----------------|------------|------------|------------|-----------|-------------|
| **LightNMEA**  | **47 579** | **21 017** | **2 795**  | **357**   | 30000/30000 |
| minmea         | 20 121     | 49 698     | 6 610      | 151       | 30000/30000 |
| TinyGPSPlus    | 17 430     | 57 369     | 7 630      | 131       | 30000/30000 |

On identical on-board packets LightNMEA is **~2.4x faster** than minmea and
**~2.7x faster** than TinyGPSPlus, with 100% successful parses for all three.

**WEMOS D1 mini ESP32 (Xtensa LX6 @ 240 MHz, xtensa-esp32-gcc 8.4.0, `-O2`)**:

| Implementation | pkt/s       | ns/pkt    | cycles/pkt | pkt/s/MHz | ok/total    |
|----------------|-------------|-----------|------------|-----------|-------------|
| **LightNMEA**  | **110 629** | **9 039** | **2 169**  | **460**   | 30000/30000 |
| minmea         | 33 955      | 29 450    | 7 068      | 141       | 30000/30000 |
| TinyGPSPlus    | 33 882      | 29 514    | 7 083      | 141       | 30000/30000 |

On identical on-board packets LightNMEA is **~3.3x faster** than both
competitors, with 100% successful parses for all three.

### Platform summary (desktop + ESP32 + RP2040)

| Platform                                              | Input                        | LightNMEA      | minmea  | TinyGPS++ | vs minmea | vs TinyGPS++ |
|-------------------------------------------------------|------------------------------|----------------|---------|-----------|-----------|--------------|
| Desktop x86_64 (GCC 14.2, `-O3`)                      | `trace_000.nmea` (10k OSM)   | 6.63 M lines/s | 1.92 M  | 80.2 k    | **3.4x**  | **82.8x**    |
| WEMOS D1 mini ESP32 (Xtensa LX6 @ 240 MHz, `-O2`)     | `nav_gen` (RMC/GGA/VTG/GLL)  | 110 629 pkt/s  | 33 955  | 33 882    | **3.3x**  | **3.3x**     |
| Raspberry Pi Pico (RP2040 @ 133 MHz, no FPU, `-O2`)   | `nav_gen` (RMC/GGA/VTG/GLL)  | 47 579 pkt/s   | 20 121  | 17 430    | **2.4x**  | **2.7x**     |

All parsers show 100% successful parses on clean data; on MCUs the advantage is consistent regardless of clock speed — LightNMEA spends 2,169 cycles/packet (ESP32) and 2,795 (RP2040) versus 7,068/6,610 for minmea and 7,083/7,630 for TinyGPS++. Absolute pkt/s on desktop and MCU are not directly comparable (different datasets and flags) — compare the ratios, not the absolute values. On the desktop, `bench_compare` additionally covers [libnmea](https://github.com/jacketizer/libnmea) via `bench_parser` (`-i ALL`, see below).

- Specific efficiency: ~1,950-4,375 sentences/MHz
- On 32-bit MCUs the integer pipeline delivers speed close to the theoretical limit
- Comparative numbers against ecosystem leaders — [minmea](https://github.com/kosma/minmea), [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus) — measured on identical data, are given in the next section

Run locally:

```bash
./bench.sh quick     # simplest: auto-build + LightNMEA-only parsing speed (100k)
./bench.sh compare   # all 4 implementations, 0% and 10% corrupted packets
./bench.sh types     # comparison by sentence type (RMC / GGA / VTG / GLL)
./bench.sh navgen    # navigation packet generation speed
# or step by step:
cd lib/LightNMEA/benchmarks
./build.sh           # builds bench_parser, bench_nav_gen
./build/bench_parser
```

## Comparison with other NMEA-0183 parsers

A head-to-head comparison against the two most well-known parsers in the NMEA-0183 ecosystem:

- **[minmea](https://github.com/kosma/minmea)** — a widely used reference parser in pure C, known as a fast and lightweight implementation
- **[TinyGPS++](https://github.com/mikalhart/TinyGPSPlus)** — the most popular Arduino GPS library

Methodology (implemented in [`bench_compare.cpp`](lib/LightNMEA/benchmarks/compare/bench_compare.cpp), full procedure below in the [Benchmarks and data](#benchmarks-and-data) section): identical input — 10,000 real OSM GPS sentences from the release dataset; identical compiler and flags (GCC 14, `-O3`); best of 5 runs; each parser achieved 100% successful parses.

| Parser             | Lines/s     | Relative to light-nmea-cpp |
|--------------------|-------------|----------------------------|
| **light-nmea-cpp** | **~5.68 M** | **1.00x**                  |
| minmea (C)         | ~1.79 M     | 0.32x                      |
| TinyGPS++ (C++)    | ~0.08 M     | 0.014x                     |

Both competitors store coordinates in floating point, and TinyGPS++ additionally consumes input character-by-character through its `encode()` state machine, which dominates its runtime. light-nmea-cpp keeps everything integer-based and slices fields via `std::string_view` in a single pass — that is why it leads on identical data, compiler, and flags.

The [`bench_parser.cpp`](lib/LightNMEA/benchmarks/bench_parser.cpp) benchmark based on an index plan (`-i ALL`, 1,000,000 packets, 5% corrupted: bad CRC + truncation) additionally covers [libnmea](https://github.com/jacketizer/libnmea) on identical input:

| Parser             | packets/s   | vs light-nmea-cpp |
|--------------------|-------------|-------------------|
| **light-nmea-cpp** | **~8.6 M**  | **1.00x**         |
| minmea (C)         | ~2.2 M      | 0.25x             |
| libnmea (C)        | ~1.8 M      | 0.20x             |
| TinyGPS++ (C++)    | ~0.12 M     | 0.014x            |

Run it yourself: `cd lib/LightNMEA/benchmarks && ./run_bench_parser.sh ALL 5 1000 RND ALL`.

## Tests

Unit tests with CTest in `lib/LightNMEA/tests`:

```bash
cd lib/LightNMEA
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Coverage: parser (`test_parser`), stream reader (`test_stream_reader`), formatters (`test_formatter`).

### Quality checks (`scripts/run_quality_checks.sh`)

One-command code quality verification — clean compilation, unit tests, fuzzing, sanitizers, no-exceptions build:

```bash
cd lib/LightNMEA
./scripts/run_quality_checks.sh             # all checks
./scripts/run_quality_checks.sh sanitize    # ASan + UBSan + leak detection
./scripts/run_quality_checks.sh fuzz 1000000
```

Current status (GCC 14.2, x86-64):

| Check                                                                                                                                             | Result                  |
|---------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------|
| C++17 compilation (`-Wall -Wextra -Wpedantic`)                                                                                                    | [OK] 0 warnings         |
| Unit tests (`test_parser`, `test_stream_reader`, `test_formatter`)                                                                                | [OK] all pass           |
| Fuzzing — deterministic (every byte of every packet: inversion/truncation/erasure/insertion) + 1,000,000 random mutations, recovery after garbage | [OK] OK                 |
| ASan + UBSan + leak check (unit tests + 200,000 fuzz mutations)                                                                                   | [OK] clean              |
| No-exceptions / no-RTTI build (`-fno-exceptions -fno-rtti`)                                                                                       | [OK] OK                 |
| valgrind memcheck                                                                                                                                 | skipped (not installed) |

The fuzz test [`tests/fuzz_parser.cpp`](lib/LightNMEA/tests/fuzz_parser.cpp) verifies coordinate invariants on every accepted packet and ensures the parser recovers from a garbage stream.

### Code footprint report (`code_report.sh`)

Proof from compiled Release code — that the parser core is free of FPU usage, dependencies, and RAM allocation,
plus the exact code size in bytes and instruction density. All numbers come from standard GNU binutils (`size`, `nm`, `objdump`, `readelf`).
The analysis is implemented in Python ([`code_report.py`](lib/LightNMEA/scripts/code_report.py)); `code_report.sh` is a convenience wrapper:

```bash
./code_report.sh                # auto: Cortex-M0+ if arm-none-eabi-g++ is available, otherwise native
./code_report.sh --m0           # build probe for Cortex-M0+ (soft-float, no FPU)
./code_report.sh --native       # build probe for the host (x86-64)
./code_report.sh --quick (-q)   # brief output: size / FPU / dependencies / heap / density
./code_report.sh <file.o|elf>   # analyze an existing object / firmware ELF
```

Report sections: (1) `.text`/`.data`/`.bss`/`.rodata` and Flash/RAM totals, (2) FPU instructions + soft-float calls, (3) dynamic `NEEDED` + external symbols, (4) heap allocation calls (`malloc`/`new`/`sbrk`/`mmap` ...), (5) code density — instruction count, average instruction width (bytes), and instructions per KiB. The probe ([`code_report_probe.cpp`](lib/LightNMEA/scripts/code_report_probe.cpp)) uses only the integer core (`parse_line` + raw values); the optional float getters and `nmea_formatter.hpp` are intentionally excluded.

Example (`--m0`, GCC 13 arm-none-eabi): code = 5011 B, Flash ~ 4.93 KiB, RAM = 0 B; FPU instructions = 0; soft-float calls = 0; allocator calls = 0; average instruction width = 2.03 B (~ the theoretical Thumb minimum), ~503 instructions/KiB. Exit code: 0 = all checks passed.

## Benchmarks and data

- `benchmarks/bench_parser.cpp` — configurable parsing speed benchmark against [minmea](https://github.com/kosma/minmea), [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus), [libnmea](https://github.com/jacketizer/libnmea) (`-i ALL`)
- `benchmarks/build_bench_parser.sh` / `benchmarks/run_bench_parser.sh` — direct gcc/g++ build and run in one command
- `benchmarks/run_benchmarks.sh` — typical scenarios (quick / compare / types / navgen / file), available from the repository root as [`bench.sh`](bench.sh)
- `benchmarks/bench_nav_gen.cpp` — navigation packet generation speed
- `benchmarks/compare/bench_compare.cpp` — head-to-head comparison with [minmea](https://github.com/kosma/minmea) and [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus) on identical real data ([`build.sh`](lib/LightNMEA/benchmarks/compare/build.sh) wraps the build)

### `bench_compare` methodology

[`bench_compare.cpp`](lib/LightNMEA/benchmarks/compare/bench_compare.cpp) measures the end-to-end throughput of three parsers on **the same input data**:

- **Input**: a `.nmea` file, one sentence per line (`bench_compare [file.nmea]`); empty lines and trailing `\r` are removed. The dataset used for the published numbers is `trace_000.nmea` from the release assets (10,000 real OSM GPS sentences).
- **Fairness**: all parsers are linked into a single binary and compiled with the same compiler and flags (`g++ -std=c++17 -O3`), so the CPU, toolchain, and optimization level are identical for every parser.
- **Metric**: sentences per second — end-to-end parsing of each line — plus the share of successfully parsed sentences (`ok/total`, %). On the real dataset all three parsers show ~100%.
- **Best of five**: each parser's throughput is measured 5 times, the minimum time is kept; each iteration recreates the parser object, so there is no warm-up bias between parsers.
- **Protection against dead-code elimination**: parsed values are accumulated into a global `volatile int64_t g_sink` so the compiler cannot discard the parsing workload.
- **Parser-specific entry points** (each parser uses its natural API):
  - LightNMEA — `Parser::parse_line()` for each line;
  - minmea — `minmea_sentence_id()` dispatch to `minmea_parse_rmc/gga/vtg/gll`;
  - TinyGPS++ — character-by-character `encode(c)` with an explicit `'\n'` terminator (mirrors real stream splitting); the result is counted via `passedChecksum()`.
- **Output**: each parser's runtime, lines/s, `ok/total` (%) and the ratio to light-nmea-cpp.

Run the comparison yourself! Fix the path to your datasets:

```bash
cd lib/LightNMEA/benchmarks/compare
./build.sh run ../../../../assets/nmea/trace_000.nmea
```

Real OpenStreetMap tracks (NMEA + original GPX) are attached to **GitHub Releases as assets** (`assets/nmea/`):

```bash
python3 scripts/download_osm_traces.py   # download raw GPX tracks
python3 scripts/gpx_to_nmea_v2.py        # convert GPX -> NMEA
```

## Repository structure

```
light-nmea-cpp/
├── bench.sh                            # Run benchmarks with one command (quick / compare / types / navgen / file)
├── build_pico.sh                       # Build/flash the RP2040 demo firmware via PlatformIO
├── code_report.sh                      # Code footprint report with one command (FPU/deps/heap/size/density)
├── platformio.ini                      # PlatformIO project (demo firmware + bundled library)
├── src/
│   ├── main.cpp                        # RP2040 Pico demo firmware: GPS logger, CSV over USB
│   └── README.md
└── lib/LightNMEA/                      # The library itself (headers only; Arduino / PlatformIO / CMake)
    ├── CMakeLists.txt                  # INTERFACE target `light_nmea`, unit tests, examples
    ├── CMakePresets.json               # CMake presets (configure/build/test)
    ├── library.json                    # PlatformIO library manifest
    ├── library.properties              # Arduino Library Manager metadata
    ├── keywords.txt                    # Arduino IDE syntax highlighting
    ├── LICENSE                         # GPL-3.0
    ├── include/
    │   ├── lightnmea.h                 # Arduino-friendly entry point (single include)
    │   ├── light_nmea.hpp              # Core: Parser, NMEAData, constexpr tables, CRC, fixed-point parsing
    │   ├── types.hpp                   # Strict integer types: Latitude, Longitude, Heading, Speed (+ FPU-free conversion)
    │   ├── utils.hpp                   # Common helper functions
    │   ├── nav_gen.hpp                 # Deterministic NMEA sentence generator (tests / benchmarks)
    │   ├── nmea_formatter.hpp          # Allocation-free formatters: TXT / CSV / JSON / Compact
    │   ├── nmea_stream_reader.hpp      # Stream reader: splitting, anti-spam filter, statistics
    │   ├── stream.hpp                  # Abstract byte source interface (read())
    │   └── serial_stream.hpp           # POSIX port implementation (UART)
    ├── examples/
    │   ├── BasicGPS/                   # Arduino: minimal GPS display
    │   ├── GPSLogger/                  # Arduino: GPS logging to SD card
    │   ├── GPSToJSON/                  # Arduino: parsing and JSON output
    │   ├── stream_reader_example.cpp   # POSIX: stream as UART + file replay
    │   ├── serial_reader_example.cpp   # POSIX: direct port reading (/dev/ttyXXX)
    │   ├── tcp_stream_example.cpp      # POSIX: NMEA over TCP (see socat demo)
    │   └── gps_daemon.cpp              # Linux: SerialStream -> Parser -> CSV daemon (-d, auto-reconnect)
    ├── tests/
    │   ├── test_parser.cpp             # Unit tests: parser behavior and regressions (incl. fields without decimal point)
    │   ├── test_stream_reader.cpp      # Unit tests: stream splitting / anti-spam / recovery
    │   ├── test_formatter.cpp          # Unit tests: TXT / CSV / JSON / Compact formatters
    │   └── fuzz_parser.cpp             # Fuzzing: deterministic + 1M random mutations, coordinate invariants
    ├── scripts/
    │   ├── run_quality_checks.sh       # Full quality control: clean build, unit tests, fuzzing, ASan/UBSan, no exceptions
    │   ├── code_report.py              # Code footprint report: size, FPU, dependencies, heap, density (binutils)
    │   ├── code_report_probe.cpp       # Probe for the footprint report (integer parser core only)
    │   ├── download_osm_traces.py      # Download real OpenStreetMap tracks for benchmark datasets
    │   ├── gpx_to_nmea_v2.py           # Convert GPX -> NMEA sentences
    │   └── socat_tcp_demo.py           # Demo: virtual port -> TCP (feeds tcp_stream_example)
    └── benchmarks/
        ├── bench_parser.cpp            # Configurable parsing speed benchmark (LIGHT / MINMEA / TINYGPS / LIBNMEA)
        ├── bench_nav_gen.cpp           # nav_gen generation speed
        ├── build_bench_parser.sh       # Direct bench_parser build with gcc/g++ (no CMake)
        ├── build_bench_fw.sh           # On-target firmware benchmark builder (RP2040 / ESP32, PlatformIO)
        ├── run_bench_parser.sh         # Run bench_parser with one command (auto-build)
        ├── run_benchmarks.sh           # Typical scenarios: quick / compare / types / navgen / file
        ├── build.sh                    # CMake build of both benchmarks
        ├── CMakeLists.txt              # Standalone benchmark project (C + C++)
        ├── compare/
        │   ├── bench_compare.cpp       # Head-to-head: LightNMEA vs minmea vs TinyGPSPlus on a real .nmea file
        │   ├── build.sh                # Build / run / clean bench_compare
        │   └── third_party/            # Vendored parsers for comparison (minmea, TinyGPS++, libnmea)
        └── firmware/                   # PlatformIO project: on-board benchmark (LightNMEA vs minmea vs TinyGPSPlus)
            ├── platformio.ini          #   env:pico (RP2040), esp32, d1_mini32, esp32s3,
            │                           #   esp32c3, esp8266, bluepill (F103C8),
            │                           #   teensy41, feather_m0 (SAMD21)
            ├── src/bench_compare_mcu.cpp
            └── lib/                    #   vendored copies of minmea + TinyGPSPlus (src/include layout)
```

## License

**GPL-3.0** — see [LICENSE](lib/LightNMEA/LICENSE).

Third-party code included for comparative benchmarks (`benchmarks/compare/third_party/`) retains its own licenses:
[minmea](lib/LightNMEA/benchmarks/compare/third_party/minmea.c) (WTFPL), [TinyGPS++](lib/LightNMEA/benchmarks/compare/third_party/TinyGPSPlus/TinyGPS++.h) (LGPL-2.1).

## Authors

Roman Shevchik — author of [light-nmea-micropython](https://github.com/octaprog7/light-nmea-micropython) and this C++ port.