# light-nmea-cpp

Ultra-fast NMEA 0183 GPS parser for embedded systems and Arduino.

> **Full repository layout** (all significant files and their purpose): see the [root README](../../README.md#repository-structure). This page covers the library itself.

## Features

- **Header-only**: No compilation needed
- **Zero allocation**: No dynamic memory allocation
- **Integer-based**: Maximum performance
- **Fast**: 11+ million packets/second on x86
- **Compact**: ~1.3 KB Flash, 52 bytes RAM
- **Modern C++ (C++17-compatible)**: constexpr lookup tables, [[likely]] branch hints, strong enums
- **Multiple formats**: TXT, CSV, JSON, Compact
- **Unit conversion**: Speed (km/h, knots, m/s), Distance (m, ft, km)
- **Supported messages**: RMC, GGA, VTG, GLL

## Installation

### Arduino IDE

1. Download as ZIP
2. Sketch -> Include Library -> Add .ZIP Library
3. Select the ZIP file

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps = LightNMEA
```

## Quick Start
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

## Integer Data Model (no floating-point)

All GNSS values are stored as **fixed-point integers** (`int32_t`) inside
strongly-typed wrappers defined in [`include/types.hpp`](include/types.hpp).
The parser itself performs **zero** `float`/`double` math, so it runs at full
speed even on MCUs without a hardware FPU (AVR, Cortex-M0+/RP2040, etc.).
Floating point is only produced on demand by the conversion helpers below.

| Type       | Scale         | Example `raw` | Physical value |
|------------|---------------|---------------|----------------|
| `Latitude` | 1e-7 ° (×10⁷) | `481173000`   | 48.117300°     |
| `Longitude`| 1e-7 ° (×10⁷) | `115167000`   | 11.516700°     |
| `Speed`    | ×1000         | `22400`       | 22.400 kn      |
| `Heading`  | 1e-2 ° (×100) | `8440`        | 84.40°         |

Valid ranges: `Latitude` ∈ [-90°·10⁷, +90°·10⁷], `Longitude` ∈ [-180°·10⁷, +180°·10⁷],
`Heading` ∈ [0, 36000) — the constructor auto-normalizes.

Fields in `light_nmea::NMEAData`: `lat`, `lon`, `speed_kn_x1000`, `speed_kmh_x1000`,
`course_true`, `course_mag`.

### How to read values — the preferred way

Use the built-in helpers; they exist exactly for this purpose:

```cpp
const auto& d = parser.data();
if (d.has_coordinates()) {
  double lat = d.latitude_deg();            // double, degrees
  double lon = d.longitude_deg();
}
float kn  = d.speed_knots();                // knots (canonical NMEA SOG unit)
float kmh = d.speed_kmh();                  // converted from knots
float ms  = d.speed_ms();                   // converted from knots
float ctr = d.course_true_deg();            // true course, degrees
float cmg = d.course_mag_deg();             // magnetic course, degrees
```

### Raw access — when you need maximum speed

Read `.raw` and convert with pure integer math (no FPU emulation):

```cpp
int32_t kmh = light_nmea::knots_x1000_to_kmh_x1000(d.speed_kn_x1000.raw); // kn × 1852 / 1000
int32_t mps = light_nmea::knots_x1000_to_ms_x1000(d.speed_kn_x1000.raw);  // kn × 463 / 900

light_nmea::Coordinate a(d.lat, d.lon), b(other.lat, other.lon);
int64_t d2 = a.distance_sq_to(b);           // squared Euclidean distance, no sqrt()
```

### Operator etiquette (what the types allow and why)

The wrappers expose **only physically meaningful operators**, all `constexpr noexcept`
inline — unused operators cost zero bytes of Flash/RAM:

- **`Latitude` / `Longitude`** — full comparison set + `is_valid()`.
  No `+`/`-`: adding two absolute positions is a category error. For deltas/distance
  use `.raw` math or `Coordinate::distance_sq_to()`.
- **`Speed`** — comparisons, `+`, `-`, `+=`, `-=`, unary `-`, `is_stopped()`.
- **`Heading`** — `+`, `-`, `+=`, `-=` with automatic normalization to [0, 36000);
  `==`, `!=`; `diff_to()` for the shortest signed angular difference.
  Deliberately **no** `<`/`>`: a circular quantity has no meaningful linear order
  (e.g. 350° is only 20° behind 10°, yet by `raw` 35000 > 1000).
- **`Coordinate`** — `==`, `!=`, `is_valid()` (also rejects the `(0,0)` origin),
  `distance_sq_to()`.

Rules of thumb:

- Prefer typed comparisons (`a == b`) over raw ones.
- Never mix units: `Speed` uses the same ×1000 scale for knots, km/h and m/s —
  compare or add values only **after** converting to the same unit
  (e.g. `knots_x1000_to_kmh_x1000`).

## Examples
* BasicGPS: Simple GPS data display
* GPSToJSON: Parse and output as JSON
* GPSLogger: Log GPS data to SD card
* gps_daemon (POSIX): `SerialStream -> NMEAStreamReader -> Parser -> CSV`
* gnss_dashboard (Linux, TUI): GNSS dashboard on curses

## GNSS Dashboard (TUI, Linux)

Interactive terminal dashboard (2x2 windows: Positioning / GNSS Parameters /
Motion Dynamics / Connection Status) reading a **serial port** (USB-CDC /
USB-UART). Rewritten from the Python originals `gnss_dashboard.py` +
`dash_utils.py` (see the MicroPython/Python version of the project):

```
SerialPort
  -> SerialParser (line buffer + detect_format())
      -> CSV stream from the board (12 fields) or RAW NMEA-0183 (light_nmea::Parser)
      -> SYS_MSG: MCU system messages (module name, software resets, watchdog)
  -> GNSS Dashboard (ncursesw)
```

The line format is detected automatically (like `detect_format()` in the Python
version): the dashboard parses both the prepared board stream (CSV, 12 comma-
separated fields) and the **raw NMEA-0183 stream** (`$XXYYY,...*HH` sentences).

Like in Python, the code is split into two parts: `examples/dash_utils.hpp`
(shared utils: GNSSData, LogWriter, AccuracyTracker, SerialParser) and
`examples/gnss_dashboard.cpp` (curses UI).

Requires `libncurses-dev` (ncursesw -- included in base Debian/Ubuntu):

```console
sudo apt install build-essential cmake libncurses-dev
cmake -S lib/LightNMEA -B lib/LightNMEA/out/build/release -DCMAKE_BUILD_TYPE=Release
cmake --build lib/LightNMEA/out/build/release --target gnss_dashboard -j
```

Or via the ready-made build script (incremental build, type selection
`Debug`/`Release`, `--clean` flag):

```console
./lib/LightNMEA/examples/build_gnss_dashboard.sh
```

Run:

```console
./lib/LightNMEA/out/build/release/gnss_dashboard                      # auto: USB-CDC @ 115200
./lib/LightNMEA/out/build/release/gnss_dashboard /dev/ttyACM0         # USB-CDC is always 115200
./lib/LightNMEA/out/build/release/gnss_dashboard /dev/ttyUSB0 9600    # USB-UART adapter
```

Exit with `q`/`Q`/`ESC`, errors go to `mcu_debug.log`, data log --
`gnss_log.csv`.

## Performance

Throughput is normalized per CPU MHz — and, inversely, as **CPU cycles per packet** —
so platforms with different clock speeds can be compared directly.

| Platform | Throughput | Normalized (pkt/s·MHz / cycles/pkt) | Flash | RAM |
|----------|-----------|--------------------------------------|-------|-----|
| x86_64 (Ryzen 7 2700, 3.2-4.1 GHz) | 7-14 M pkt/s [1] | ~2,000-4,400 pkt/s·MHz (≈230-510 cycles/pkt) | — | — |
| Cortex-M0+ (RP2040 @ 133 MHz) | 5-10× faster than float parsers [2] | measure on target [3] | ~1.3 KB | 52 B |

[1] Measured with [`bench_parser`](benchmarks/bench_parser.cpp) (GCC 14.2, `-O3 -march=native`,
mixed RMC/GGA/VTG/GLL): 7.15 M pkt/s average (8.2 M pkt/s on input with 5% corrupted
packets) on the repo machine; 11-14 M pkt/s on the reference rig (see
[root README](../../README.md#performance)). ±20% spread due to turbo/load.

[2] Relative to float-based parsers ([minmea](https://github.com/kosma/minmea),
[TinyGPS++](https://github.com/mikalhart/TinyGPSPlus)): Cortex-M0+ has no FPU, so a
`double` multiply costs ~50+ soft-FPU instructions, while the integer pipeline here
does the same work with a few ALU ops.

[3] No on-target C++ measurement is stored in this repo yet — fill this cell by
benchmarking on your board.

# License
GPL v3.0