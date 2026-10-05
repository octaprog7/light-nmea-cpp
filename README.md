# light-nmea-cpp

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Language](https://img.shields.io/badge/C%2B%2B-20-brightgreen)
![Version](https://img.shields.io/badge/version-1.0.0-orange)
![Platform](https://img.shields.io/badge/platform-embedded%20%7C%20desktop-lightgrey)

Ultra-fast, zero-allocation **NMEA 0183** GPS parser written in modern C++ (**C++20 by default, fully C++17-compatible**).

The library targets **32-bit microcontrollers** (Cortex-M, ESP32, STM32, RP2040): an integer-only pipeline that **never touches the FPU**, **no block allocation from RAM on the hot path** — raw parse speed as close to the silicon limit as the hardware allows. At the same time it runs in high-rate telemetry pipelines on the desktop — and it is ready for 8-bit AVR boards the moment your toolchain reaches the required C++ standard (see the note for 8-bit users below).

## Motivation

After shipping **[light-nmea-micropython](https://github.com/octaprog7/light-nmea-micropython)**, we found the C++ side of the niche still wide open. Most NMEA parsers out there are written for Python or target legacy Arduino-era C, leaving modern C++ developers without a truly fast, zero-allocation option.

The task was set for **32-bit microcontrollers**: achieve performance as close to the practically possible limit as possible **without using the FPU** — pure integer arithmetic from sentence tokenization to coordinate math and formatting. And **no block allocation from RAM on the hot path**: while parsing, the library must never touch the heap, so its memory footprint stays strictly bounded at compile time and fits any modern MCU.

light-nmea-cpp closes that gap. Its design and core parsing logic are derived directly from **light-nmea-micropython** — battle-tested in real projects — reimplemented in modern C++ (C++20 by default, fully C++17-compatible). It brings the fastest NMEA parsing to people who write C++ for a living — and who wouldn't trade their toolchain for a Python script. No runtime allocation, no floating-point on the hot path, just raw parse speed where you need it: on a 32-bit microcontroller or in a high-rate telemetry pipeline.

## What it gives you

**Never the bottleneck.** At 5–14 million sentences per second, parsing stops being a cost you think about. On a 32-bit MCU the parser runs tens to hundreds of times faster than the GPS module can feed it — your firmware spends its cycles on your application, not on NMEA.

**No memory failures.** Zero heap allocation on the hot path means no `malloc` failures, no fragmentation, no out-of-memory reboots — even on devices meant to run for months or years. Memory usage is fully known at compile time.

**Fits anywhere.** ~1.3 KB of Flash and 52 bytes of RAM let the parser coexist with a full application on a small MCU — or disappear entirely inside a big one.

**Deterministic by design.** Bounded buffers, no recursion, no hidden state. The same input always produces the same output with the same latency — predictable behavior you can rely on in real-time systems.

**The compiler guards your math.** Coordinates, speeds and headings are strong types, not raw floats: mixing knots with km/h becomes a compile error, not a silent bug. Integer storage also means identical results on every platform — no float rounding surprises.

**Zero setup.** One header, include and go — for Arduino, PlatformIO, CMake, whatever you already use.

**Out-of-the-box output.** TXT, CSV, JSON and Compact formatters are included, plus unit conversion — you don't re-invent output code for logging or telemetry.

**From AVR to x86_64.** The same source runs on an 8-bit AVR, a Cortex-M, an ESP32 and a desktop server. Write once, deploy anywhere.

## Features

- **Header-only** — drop in, no compilation step, no link-time dependencies
- **Built for 32-bit MCUs** — integer-only pipeline, no FPU involvement, near-limit throughput
- **Zero allocation on the hot path** — no `malloc`/`new`, no heap block allocation while parsing; memory footprint bounded at compile time
- **Zero copy** — `std::string_view` tokenization, no intermediate substring objects
- **Integer-based** — coordinates stored as scaled integers (1e-7°), no FPU required
- **Fast** — 11+ million sentences/second on x86_64; on identical data measured **3.2× faster** than minmea and **~70× faster** than TinyGPS++ (see [Comparison](#comparison-with-other-nmea-0183-parsers))
- **Compact** — ~1.3 KB Flash, 52 bytes RAM on Cortex-M0+
- **Supported messages** — RMC, GGA, VTG, GLL
- **Constellation-aware** — Talker ID → GPS / GLONASS / Galileo / BeiDou / QZSS / NavIC / Multi, with bitmask filtering (`CST_MASK_*`)
- **Multiple output formats** — TXT, CSV, JSON, Compact
- **Unit conversion** — speed (km/h, knots, m/s), distance (m, ft, km)
- **Streaming mode** — `NMEAStreamReader` with anti-spam filtering and statistics
- **Compile-time tables** — `constexpr` lookup tables for Talker ID routing and CRC validation
- **Modern C++ (C++17-compatible)** — `constexpr` lookup tables, `[[likely]]` branch hints, strong enum types, no RTTI/exceptions required

## Requirements

| Compiler | Minimum version |
|----------|-----------------|
| GCC      | ≥ 9 (C++17 or C++20) |
| Clang    | ≥ 12 (C++17 or C++20) |
| MSVC     | VS 2019 16.10+         |

- Standard: **C++20** recommended; the library is **fully C++17-compatible** — a clean `-std=c++17` build compiles warning-free (verified with `-Wall -Wextra -Wpedantic` on GCC 14) with no measurable performance difference.
- Recommended flags for MCUs: `-fno-exceptions -fno-rtti -Os` (fully supported)
- Platforms: 32-bit MCUs (Cortex-M, ESP32, ESP8266, STM32, RP2040), Arduino (AVR, SAMD), and any POSIX desktop

> **Note for 8-bit MCU users (AVR, PIC)**
>
> Nothing in light-nmea-cpp requires 32-bit hardware: the parsing pipeline is pure integer math — no FPU, no heap, no division on the hot path — so it is a natural fit for 8-bit microcontrollers too. The only thing standing between you and this library is your compiler: it needs C++17 features such as `std::string_view` (the `[[likely]]`/`[[unlikely]]` hints are C++20, but GCC ≥ 9 and Clang ≥ 12 accept them in C++17 mode as harmless extensions).
>
> If your 8-bit toolchain is still below C++17, upgrading the compiler is all that's needed: recent avr-gcc releases already support C++17, and the core contains no 32-bit-specific code — the parser just needs a C++17-capable compiler.

## Why C++20 and not C++17 or C++23?

**Why not C++17?**

- `[[likely]]` / `[[unlikely]]` are not part of C++17. Before C++20 they existed only as compiler-specific extensions (`__builtin_expect`). These branch-prediction hints sit on the parser's hot path — exactly where the last few percent of MCU throughput comes from. In practice this is not a blocker: GCC ≥ 9 and Clang ≥ 12 accept the attributes in C++17 mode as extensions, so the hints keep working.
- The compile-time lookup tables deliberately use only C++17 features (`constexpr`, `std::array`); the library contains no `std::span` and no other C++20-only constructs — a clean `-std=c++17` build is fully supported, not a degraded fallback.
- Benchmark on x86-64 shows identical throughput for C++17 and C++20 builds within measurement noise (±5%), so nothing is lost by targeting C++17 where a toolchain requires it (e.g. PlatformIO for RP2040).

**Why not C++23?**

- C++23 was simply too young: at development time no major MCU toolchain — arm-none-eabi-gcc, the ESP32/STM32/RP2040 Arduino cores, avr-gcc — had complete C++23 support.
- The features that would have helped (`std::to_underlying`, `std::expected`) are conveniences, not necessities; the library ships its own minimal `to_underlying()`.

**Why C++20?**

- It is the newest standard with complete, stable support in every toolchain we target: GCC ≥ 10, Clang ≥ 12, MSVC 2019 16.10+, and the GCC-based toolchains of Cortex-M, ESP32 and RP2040.
- It brings the performance-relevant `[[likely]]` / `[[unlikely]]` attributes while remaining compatible with C++17 mode when a project needs it.

**In short: C++20 is the best ratio of performance features to real-world compiler support.** The hot path gets its branch hints without sacrificing any target platform.

## Installation

### Arduino IDE

1. Download this repository as **ZIP** (Code → Download ZIP)
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library**
3. Select the downloaded ZIP
4. Use `#include <lightnmea.h>`

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps = octaprog7/light-nmea-cpp
```

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

## Quick Start

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

## API Reference

### `light_nmea::Parser`

Main entry point. Stateless per sentence, designed to be reused.

| Member | Description |
|--------|-------------|
| `bool parse_line(std::string_view line)` | Parse one NMEA sentence (with `$`, checksum `*XX`). Returns `true` on success |
| `const NMEAData& data() const` | Access parsed data of the last successful sentence |
| `struct Config` | Tunables, e.g. `trust_gga_fix` |
| `struct Diagnostics` | Counters, e.g. `reject_crc` |

### `NMEAData` and type-safe types

Parsed fields are stored in strong, integer-based types from `types.hpp`:
`Latitude`, `Longitude`, `Heading`, `Speed` (raw `int32_t`, scale 1e-7 for coordinates).

Key accessors:

| Accessor | Returns |
|----------|---------|
| `latitude_deg()` / `longitude_deg()` | double degrees |
| `speed_kmh()` / `speed_knots()` / `speed_ms()` | converted speed |
| `course_true_deg()` | heading in degrees |
| `altitude_m()` | altitude in meters |
| `hdop_value()` | HDOP value |
| `has_coordinates()` | validity of lat/lon |

Other fields: `time_utc`, `date`, `satellites`, `valid`, `constellation` (`Constellation`), `fix_mode` (`FixMode`).

### Constants & filtering

| Constant | Meaning |
|----------|---------|
| `MAX_PACKET_SIZE` (120) | max sentence length accepted |
| `MAX_COMMAS` (14) | max fields |
| `CST_MASK_GPS … CST_MASK_ALL` | constellation bitmasks for `set_cst_filter()` |

`MessageType`: `RMC`, `GGA`, `VTG`, `GLL`.
`FixMode`: `Autonomous`, `DGPS`, `Estimated`, `NotValid`, `RTKFixed`, `RTKFloat`.

## Formatters

`nmea_formatter.hpp` renders parsed data without allocations (stack `BufferWriter`):

```cpp
Format::TXT | Format::CSV | Format::JSON | Format::COMPACT
```

Unit enums: `SpeedUnit { KMH, KNOTS, MS }`, `DistanceUnit { METERS, FEET, KILOMETERS }`.
Format via `data.to_txt()`, `data.to_csv()`, `data.to_json()`, `data.to_compact()`, or unified `data.to_format(format)`.

## Streaming

For continuous data sources (UART, TCP, files):

- **`NMEAStreamReader`** — automatic sentence framing + anti-spam filter (GGA/RMC), collects `Stats { packets_processed, … }`
- **`Stream`** — abstract `read()` interface for any byte source
- **`SerialStream`** — POSIX serial port (`/dev/ttyACM*`, `/dev/ttyUSB*`)
- **`FileStream`** — replay of recorded `*.nmea` tracks

Example: `examples/tcp_stream_example.cpp` + `scripts/socat_tcp_demo.py` demonstrate the full chain `socat (PTY→TCP) → TCP client → Parser`.

## Examples

| Example | Platform | Description |
|---------|----------|-------------|
| [`BasicGPS.ino`](lib/LightNMEA/examples/BasicGPS/BasicGPS.ino) | Arduino | Minimal GPS display |
| [`GPSToJSON.ino`](lib/LightNMEA/examples/GPSToJSON/GPSToJSON.ino) | Arduino | Parse and emit JSON |
| [`GPSLogger.ino`](lib/LightNMEA/examples/GPSLogger/GPSLogger.ino) | Arduino | Log GPS data |
| [`stream_reader_example.cpp`](lib/LightNMEA/examples/stream_reader_example.cpp) | POSIX | UART-like + file streaming |
| [`serial_reader_example.cpp`](lib/LightNMEA/examples/serial_reader_example.cpp) | POSIX | Serial port reader |
| [`tcp_stream_example.cpp`](lib/LightNMEA/examples/tcp_stream_example.cpp) | POSIX | TCP stream (see socat demo) |
| [`gps_daemon.cpp`](lib/LightNMEA/examples/gps_daemon.cpp) | Linux | GPS daemon: serial → CSV file, daemonize (`-d`), auto-reconnect |

The [**gps_daemon**](lib/LightNMEA/examples/gps_daemon.cpp) example shows a complete Linux pipeline — `SerialStream → NMEAStreamReader → Parser → to_csv()`: it reads a real serial device (`/dev/ttyACM*`, `/dev/ttyUSB*`), writes one CSV line per valid fix, survives device replugs, responds to SIGINT/SIGTERM and can run in the background:

```bash
# foreground: device, baud, output file
./gps_daemon /dev/ttyACM0 9600 /tmp/gps.csv

# background daemon
./gps_daemon -d /dev/ttyUSB0 38400 /var/log/gps.csv
```

To test it without hardware, feed recorded NMEA through a virtual serial port:

```bash
socat -d -d PTY,link=/tmp/gps_pty,raw,echo=0 \
      SYSTEM:"cat assets/nmea/trace_000.nmea; sleep 2" &
./gps_daemon -s 1 /tmp/gps_pty 9600 /tmp/gps_out.csv
```

A ready-to-flash firmware demo (RP2040 Pico GPS logger, CSV over USB) lives in [`src/main.cpp`](src/main.cpp) — build with PlatformIO: `pio run -t upload` (see [`build_pico.sh`](build_pico.sh)).

## Performance

Measured on real hardware with the included benchmarks (`bench_parser`):

| Platform | Speed | Flash | RAM |
|----------|-------|-------|-----|
| x86_64 (Ryzen 7 2700) | 11–14 M pkt/s | — | — |
| Cortex-M0+ | 5–10× faster than float parsers | ~1.3 KB | 52 B |
| ESP32 | 11+ M pkt/s | ~1.3 KB | 52 B |

- Specific efficiency: ~1,950–4,375 sentences/MHz
- On 32-bit MCUs the integer-only pipeline delivers throughput close to the theoretical limit — no FPU, no division, no heap access on the hot path
- Head-to-head numbers vs the ecosystem leaders — [minmea](https://github.com/kosma/minmea), [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus) — measured on identical data are in the next section

Run locally:

```bash
cd lib/LightNMEA/benchmarks
./build.sh          # builds bench_parser, bench_nav_gen
./build/bench_parser
```

## Comparison with other NMEA-0183 parsers

Head-to-head measurement against the two most representative parsers in the NMEA-0183 ecosystem:

- **[minmea](https://github.com/kosma/minmea)** — the widely used pure-C reference parser, known as a fast and lean implementation
- **[TinyGPS++](https://github.com/mikalhart/TinyGPSPlus)** — the most popular Arduino GPS library

Methodology: identical input — 10,000 real OSM GPS sentences from the release dataset; identical compiler and flags (GCC 14, `-O3`); best of 5 runs; every parser reported 100% successful parses.

| Parser | Lines/s | Relative to light-nmea-cpp |
|--------|---------|----------------------------|
| **light-nmea-cpp** | **~5.68 M** | **1.00×** |
| minmea (C) | ~1.79 M | 0.32× |
| TinyGPS++ (C++) | ~0.08 M | 0.014× |

Both competitors store coordinates as floating point, and TinyGPS++ additionally consumes input character-by-character through its `encode()` state machine, which dominates its runtime. light-nmea-cpp keeps everything integer-based and slices fields with `std::string_view` in a single pass — which is why it leads on the same data, compiler and flags.

The plan-based benchmark [`bench_parser.cpp`](lib/LightNMEA/benchmarks/bench_parser.cpp) (`-i ALL`, 1,000,000 packets, 5% corrupted: bad CRC + truncated) additionally covers [libnmea](https://github.com/jacketizer/libnmea) on identical input:

| Parser | pkt/s | vs light-nmea-cpp |
|--------|-------|-------------------|
| **light-nmea-cpp** | **~8.6 M** | **1.00×** |
| minmea (C) | ~2.2 M | 0.25× |
| libnmea (C) | ~1.8 M | 0.20× |
| TinyGPS++ (C++) | ~0.12 M | 0.014× |

Run it yourself: `cd lib/LightNMEA/benchmarks && ./run_bench_parser.sh ALL 5 1000 RND ALL`.

## Tests

CTest-based unit tests in `lib/LightNMEA/tests`:

```bash
cd lib/LightNMEA
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Coverage: parser (`test_parser`), stream reader (`test_stream_reader`), formatters (`test_formatter`).

### Quality checks (`scripts/run_quality_checks.sh`)

One-command verification of execution quality — clean compile, unit tests, fuzzing, sanitizers, no-exceptions build:

```bash
cd lib/LightNMEA
./scripts/run_quality_checks.sh             # all checks
./scripts/run_quality_checks.sh sanitize    # ASan + UBSan + leak detection
./scripts/run_quality_checks.sh fuzz 1000000
```

Current status (GCC 14.2, x86-64):

| Check | Result |
|-------|--------|
| Clean C++17 compile (`-Wall -Wextra -Wpedantic`) | ✅ 0 warnings |
| Unit tests (`test_parser`, `test_stream_reader`, `test_formatter`) | ✅ all pass |
| Fuzz — deterministic (every byte of every packet: flip/truncate/erase/insert) + 1,000,000 random mutations, recovery after garbage | ✅ OK |
| ASan + UBSan + leak check (unit tests + 200,000 fuzz mutations) | ✅ clean |
| No-exceptions / no-RTTI build (`-fno-exceptions -fno-rtti`) | ✅ OK |
| valgrind memcheck | skipped (not installed) |

The fuzz test [`tests/fuzz_parser.cpp`](lib/LightNMEA/tests/fuzz_parser.cpp) validates coordinate invariants on every accepted packet and verifies the parser recovers after a garbage stream.

## Benchmarks & Data

- `benchmarks/bench_parser.cpp` — configurable throughput benchmark vs [minmea](https://github.com/kosma/minmea), [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus), [libnmea](https://github.com/jacketizer/libnmea) (`-i ALL`)
- `benchmarks/build_bench_parser.sh` / `benchmarks/run_bench_parser.sh` — direct gcc/g++ build and one-command run
- `benchmarks/bench_nav_gen.cpp` — navigation generator throughput
- `benchmarks/compare/` — head-to-head vs minmea and TinyGPS++ on real data

Reproduce the comparison yourself (adjust the dataset path to your copy of the release assets):

```bash
cd lib/LightNMEA/benchmarks/compare
./build.sh run ../../../../assets/nmea/trace_000.nmea
```

Real-world OpenStreetMap tracks (NMEA + raw GPX sources) are attached to **GitHub Releases as assets** (`assets/nmea/`), not stored in the repository. To regenerate them:

```bash
python3 scripts/download_osm_traces.py   # fetch raw GPX tracks
python3 scripts/gpx_to_nmea_v2.py        # convert GPX → NMEA
```

## License

**GPL-3.0** — see [LICENSE](lib/LightNMEA/LICENSE).

Third-party code included for comparative benchmarks (`benchmarks/compare/third_party/`) keeps its own licenses:
[minmea](lib/LightNMEA/benchmarks/compare/third_party/minmea.c) (WTFPL), [TinyGPS++](lib/LightNMEA/benchmarks/compare/third_party/TinyGPSPlus/TinyGPS++.h) (LGPL-2.1).

## Authors

Roman Shevchik — author of [light-nmea-micropython](https://github.com/octaprog7/light-nmea-micropython) and this C++ port.
