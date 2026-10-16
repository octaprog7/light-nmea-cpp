# light-nmea-cpp

[![CI](https://github.com/octaprog7/light-nmea-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/octaprog7/light-nmea-cpp/actions/workflows/ci.yml)
[![Docs](https://github.com/octaprog7/light-nmea-cpp/actions/workflows/pages.yml/badge.svg)](https://github.com/octaprog7/light-nmea-cpp/actions/workflows/pages.yml)

An ultra-fast NMEA 0183 (GPS) parser for embedded systems and Arduino.

> **Full repository structure** (all files and their purpose): see the [root README](../../README.md#repository-structure). This document describes the library.

## Features

- **Header-only**: header files only (plus an optional single-header variant, `lightnmea_single.hpp`)
- **No dynamic heap allocation**: does not use the heap
- **Integer arithmetic**: does not use the FPU
- **High speed**: 11+ million packets per second on x86 (R7 2700)
- **Small footprint**: ~1.3 KB of Flash, 52 bytes of RAM
- **Modern C++ (C++17-compatible)**: `constexpr` lookup tables, `[[likely]]` branch hints, strict enums (`enum class`)
- **C API** (`lightnmea_c.h`): usable from plain C projects
- **ESP-IDF component**: `esp-idf-component/` (no Arduino required)
- **Multiple output formats**: TXT, CSV, JSON, Compact
- **Unit conversion**: speed (km/h, knots, m/s), distance (m, feet, km)
- **Supported messages/sentences**: RMC, GGA, VTG, GLL

## Installation

### Arduino IDE

1. Download the repository ZIP (Code -> Download ZIP) and extract it
2. Copy the `lib/LightNMEA` folder into your Arduino `libraries` directory
   (e.g. `~/Arduino/libraries/LightNMEA`)
3. Restart the Arduino IDE: the examples appear under
   File -> Examples -> light-nmea-cpp

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps = https://github.com/octaprog7/light-nmea-cpp.git
```

Or build the bundled examples directly from the repository
(each example has its own `platformio.ini`):

```console
cd lib/LightNMEA/examples/BasicGPS
pio run -e pico          # or: esp32dev, bluepill
pio run -t upload -e pico
```

## Quick start
```console
#include <lightnmea.h>

lightnmea::Parser parser;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);  // GPS module
}

void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (c == '\n') {
      if (parser.parse_line(buffer)) {
        const auto& data = parser.data();
        
        if (data.has_coordinates()) {
          Serial.print("Lat: ");
          Serial.println(data.latitude_deg(), 6);
          Serial.print("Lon: ");
          Serial.println(data.longitude_deg(), 6);
        }
      }
    }
  }
}
```

## Integer data model

All GNSS values are stored as **fixed-point integers** (`int32_t`) in strictly typed wrappers defined in [`include/types.hpp`](include/types.hpp).
The parser itself **never performs** any `float` or `double` computations, so it runs at maximum
speed even on microcontrollers without a hardware FPU (AVR, Cortex-M0+/RP2040, etc.).
Conversion to floating point is performed only on demand via the helper functions described below.

| Type        | Scale          | Example `raw` | Physical value |
|------------|----------------|---------------|----------------|
| `Latitude` | 1e-7 ° (×10⁷) | `481173000`   | 48.117300°     |
| `Longitude`| 1e-7 ° (×10⁷) | `115167000`   | 11.516700°     |
| `Speed`    | ×1000          | `22400`       | 22.400 knots   |
| `Heading`  | 1e-2 ° (×100)  | `8440`        | 84.40°         |

Valid ranges: `Latitude` -> [-90°·10⁷, +90°·10⁷], `Longitude` -> [-180°·10⁷, +180°·10⁷],
`Heading` -> [0, 36000) — the constructor performs automatic normalization.

Fields in `light_nmea::NMEAData`: `lat`, `lon`, `speed_kn_x1000`, `speed_kmh_x1000`,
`course_true`, `course_mag`.

### How to read values — the recommended way

Use the built-in helper functions:

```cpp
const auto& d = parser.data();
if (d.has_coordinates()) {
double lat = d.latitude_deg(); // double, degrees
double lon = d.longitude_deg();
}
float kn  = d.speed_knots(); // knots (the SOG unit in NMEA)
float kmh = d.speed_kmh(); // converted from knots
float ms  = d.speed_ms(); // converted from knots
float ctr = d.course_true_deg(); // true course, degrees
float cmg = d.course_mag_deg(); // magnetic course, degrees
```

### Direct access and integer arithmetic: maximum performance for GNSS computations

Read the `.raw` field and convert using integer arithmetic (no FPU emulation):

```cpp
int32_t kmh = light_nmea::knots_x1000_to_kmh_x1000(d.speed_kn_x1000.raw); // knots × 1852 / 1000
int32_t mps = light_nmea::knots_x1000_to_ms_x1000(d.speed_kn_x1000.raw); // knots × 463 / 900

light_nmea::Coordinate a(d.lat, d.lon), b(other.lat, other.lon);
int64_t d2 = a.distance_sq_to(b); // squared Euclidean distance, without sqrt()
```

### Operator usage rules (what is allowed for the types and why)

The wrappers provide **only physically meaningful operators**; all of them are marked `constexpr noexcept`
and `inline` — unused operators take no space in Flash or RAM:

- **`Latitude` / `Longitude`** — a full set of comparison operators + `is_valid()`.
`+` and `-` are absent: adding two absolute coordinates is a logical error (mixing concepts). For deltas or distances
use the "raw" values or the `Coordinate::distance_sq_to()` method.
- **`Speed`** — comparison operators, `+`, `-`, `+=`, `-=`, unary `-`, `is_stopped()`.
- **`Heading`** — `+`, `-`, `+=`, `-=` with automatic normalization to the range [0, 36000);
`==`, `!=`; `diff_to()` for computing the shortest signed angular difference.
`<`/`>` operators are **intentionally absent**: for a cyclic quantity the notion of a linear order is meaningless
(e.g., 350° lags behind 10° by only 20°, whereas in "raw" form 35000 > 1000).
- **`Coordinate`** — `==`, `!=`, `is_valid()` (also rejects the origin `(0,0)`),
`distance_sq_to()`.

Key recommendations:

- Prefer typed comparisons (`a == b`) over comparing "raw" values.
- Never mix units: `Speed` uses a single scale (value ×1000) for knots, km/h and m/s — compare or add values only **after** converting to the same unit, e.g. via `knots_x1000_to_kmh_x1000`.

## Examples
* BasicGPS: Simple GPS data output
* GPSToJSON: Parsing data and output in JSON format
* GPSLogger: Writing GPS data to an SD card
* gps_daemon (POSIX): `SerialStream -> NMEAStreamReader -> Parser -> CSV`
* gnss_dashboard (Linux, TUI): curses-based GNSS dashboard
* freertos (host, POSIX port): producer/consumer tasks + FreeRTOS queue
* zephyr (Zephyr RTOS): `uart_pipe` -> ring buffer -> `NMEAStreamReader`

## RTOS support (FreeRTOS / Zephyr)

The library core (`light_nmea.hpp`, `nmea_stream_reader.hpp`,
`nmea_formatter.hpp`) is **pure C++17** and does not depend on Arduino or
POSIX APIs, so it works in any RTOS task without changes. To integrate, you
only need to implement the abstract `light_nmea::Stream` interface
(`read()` method) over your UART driver or RTOS queue.

Bundled examples:

* [`examples/freertos/`](examples/freertos/) — FreeRTOS on the host
  (POSIX port, no hardware required): a producer task reads an NMEA track
  from a file and sends bytes to a FreeRTOS queue; a consumer task parses
  them via `NMEAStreamReader` + `Parser`.
  ```console
  cd lib/LightNMEA/examples/freertos
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  ./build/freertos_nmea_example
  ```

* [`examples/zephyr/`](examples/zephyr/) — Zephyr application: reads NMEA
  from UART via `uart_pipe`, accumulates bytes in a ring buffer and parses
  them in the main loop. Works on `native_sim` (input via terminal) and on
  real boards (build verified for nRF52840 DK, see `boards/` overlay).
  ```console
  cd lib/LightNMEA/examples/zephyr
  west build -b nrf52840dk/nrf52840 -d build/nrf52840dk   # real board
  west build -b native_sim -d build/native_sim            # host (needs gcc-multilib)
  ```

## GNSS dashboard (TUI, Linux)

An interactive terminal dashboard (4-window layout: positioning / GNSS parameters /
motion dynamics / connection status) that reads data via a **serial port** (USB-CDC /
USB-UART). Rewritten from the Python originals — `gnss_dashboard.py` and
`dash_utils.py` (see the MicroPython/Python version of the project):

```
SerialPort
-> SerialParser (line buffer + detect_format())
-> CSV stream from the board (12 fields) or "raw" NMEA-0183 stream (light_nmea::Parser)
-> SYS_MSG: Microcontroller system messages (module name, software resets, watchdog)
-> GNSS Dashboard (ncursesw)
```

The line format is detected automatically (similar to the `detect_format()` function in the
Python version): the dashboard recognizes both the prepared data stream from the board (CSV, 12 fields
separated by commas) and the **"raw" NMEA-0183 stream** (messages of the form `$XXYYY,...*HH`).

As in the Python version, the code is split into two parts: `examples/dash_utils.hpp`
(common utilities: GNSSData, LogWriter, AccuracyTracker, SerialParser) and
`examples/gnss_dashboard.cpp` (curses-based interface). Requires `libncurses-dev` (ncursesw is included in the base Debian/Ubuntu distribution):

```console
sudo apt install build-essential cmake libncurses-dev
cmake -S lib/LightNMEA -B lib/LightNMEA/out/build/release -DCMAKE_BUILD_TYPE=Release
cmake --build lib/LightNMEA/out/build/release --target gnss_dashboard -j
```

Or use the ready-made build script (incremental build, `Debug`/`Release` mode selection, `--clean` flag):

```console
./lib/LightNMEA/examples/build_gnss_dashboard.sh
```

Run:

```console
./lib/LightNMEA/out/build/release/gnss_dashboard                      # auto: USB-CDC at 115200
./lib/LightNMEA/out/build/release/gnss_dashboard /dev/ttyACM0         # USB-CDC always runs at 115200
./lib/LightNMEA/out/build/release/gnss_dashboard /dev/ttyUSB0 9600    # USB-UART adapter
```

Exit with `q`/`Q`/`ESC`; errors are written to `mcu_debug.log`, the data log —
to `gnss_log.csv`.

## Performance

Throughput is normalized to the CPU clock frequency (MHz) — and, conversely, expressed as **CPU cycles per packet** —
which allows direct comparison of platforms with different clock frequencies.

| Platform | Throughput | Normalized metrics (packets/s·MHz / cycles/packet) | Flash | RAM |
|----------|-----------|--------------------------------------|-------|-----|
| x86_64 (Ryzen 7 2700, 3.2–4.1 GHz) | 7–14 M packets/s [1] | ~2000–4400 packets/s·MHz (≈230–510 cycles/packet) | — | — |
| Cortex-M0+ (RP2040, 133 MHz) | 5–10x faster than floating-point parsers [2] | Measured on the target device [3] | ~1.3 KB | 52 B |

[1] Measured with [`bench_parser`](benchmarks/bench_parser.cpp) (GCC 14.2, `-O3 -march=native`,
mixed RMC/GGA/VTG/GLL stream): on average 7.15 M packets/s (8.2 M packets/s with an input stream
containing 5% corrupted packets) on the machine hosting the repository; 11–14 M packets/s
on the reference bench (see the [root README](../../README.md#performance)).
The ±20% spread is due to Turbo mode and system load.

[2] Compared with parsers that use floating-point arithmetic ([minmea](https://github.com/kosma/minmea),
[TinyGPS++](https://github.com/mikalhart/TinyGPSPlus)): the Cortex-M0+ has no FPU, so multiplying
`double` values requires more than 50 instructions of software FPU emulation,
whereas the integer pipeline performs the same task in a few ALU operations.

[3] Take measurements on your own board using the dedicated comparative benchmark
firmware ([`benchmarks/firmware/`](benchmarks/firmware/)) — see the next section.

## Microcontroller benchmark (RP2040 / ESP32)

The repository includes a PlatformIO firmware that lets you compare **LightNMEA** with the
[minmea](https://github.com/kosma/minmea) and
[TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) libraries **on the same board**
([`benchmarks/firmware/`](benchmarks/firmware/),
source code: [`src/bench_compare_mcu.cpp`](benchmarks/firmware/src/bench_compare_mcu.cpp)).
The firmware generates packets with the generator from `nav_gen.hpp` (RMC/GGA/VTG/GLL types) and outputs the best performance results (out of N iterations) via the serial port (115200 baud) in units of: packets/s, ns/packet, cycles/packet and packets/(s·MHz) (which allows direct comparison of systems with different clock frequencies).

Requirements: PlatformIO (version 6.x).

```console
cd benchmarks
./build_bench_fw.sh pico            # build firmware for RP2040 (available: esp32, all)
./build_bench_fw.sh pico upload     # flash the board
./build_bench_fw.sh esp32 monitor   # view results (reset the board to re-run)
```

The amount of data can be configured with the `BENCH_PACKET_COUNT` build flag (default 30,000),
and the number of passes with `BENCH_PASSES` (default 3; the best result is taken):

```console
BENCH_PACKET_COUNT=100000 ./build_bench_fw.sh pico
```

The same benchmark is also available for **RTOS environments**: a FreeRTOS
(POSIX port, host) and a Zephyr (native_sim / nRF52840 DK) version share the
same portable core ([`benchmarks/rtos/`](benchmarks/rtos/),
source code: [`bench_compare_rtos.hpp`](benchmarks/rtos/bench_compare_rtos.hpp)) —
only the time source and output differ (`clock_gettime` / `k_cycle_get_32()`,
`printf` / `printk`).

> **Note on firmware size.** The test application itself takes ~9 KB,
> but the Arduino core for ESP32 (ESP-IDF HAL + newlib stdio) adds
> a fixed ~280 KB regardless of the application code: that is about **22% of
> the 1.31 MB application partition** compared to ~3.7% (78 KB) with the Pico SDK.
> These are platform overheads, not parser code — the LightNMEA library itself takes
> ~1.3 KB of Flash and 52 bytes of RAM. Do not directly compare the absolute numbers
> of the two boards; use the normalized metrics (cycles/packet, packets/s·MHz)
> that the benchmark outputs.

# License
GPL v3.0