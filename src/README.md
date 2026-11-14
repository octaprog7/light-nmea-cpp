# LightNMEA GNSS Logger - Cross-Platform Firmware

## Overview

[`main.cpp`](main.cpp) is a **cross-platform** Arduino firmware that:

- Receives NMEA 0183 sentences from a GNSS module via a serial port
  (hardware UART or SoftwareSerial, depending on the board)
- Parses the data with the high-performance, integer-only
  [LightNMEA](../lib/LightNMEA/README.md) library
- Outputs one clean CSV line per second to the USB serial port (`Serial`)
  at **115200 baud** (GNSS port, runs at **38400 baud**)

### Supported Platforms

The platform is auto-detected at compile time via preprocessor macros:

| Platform | GNSS port | GNSS RX / TX pins | Debug output |
|----------|----------|------------------|--------------|
| Raspberry Pi Pico (RP2040) | `Serial1` | GPIO 1 / 0 | `Serial` (USB-CDC) |
| STM32 (Blue Pill, Nucleo)  | `Serial1` | PA3 / PA2 | `Serial` (USB) |
| ESP32                      | `Serial1` | GPIO 16 / 17 | `Serial` (USB) |
| ESP8266                    | `Serial`  | GPIO 3 / 1 | disabled (port shared with GNSS) |
| Arduino Mega (AVR)         | `Serial1` | 19 / 18 | `Serial` (USB) |
| Arduino Uno / Nano (AVR)   | SoftwareSerial | 5 / 6 | `Serial` (USB) |
| SAMD (Zero, Nano 33)       | `Serial1` | 13 / 14 | `Serial` (USB) |

Unsupported boards fail at compile time with `#error`.

## Features

- **High-performance parsing**: integer-only fixed-point math, zero heap
  allocations, zero `float`/`double` inside the parser (~1.3 KB Flash,
  52 bytes RAM for the library core)
- **Type-safe data model**: strongly-typed `Latitude`/`Longitude`/`Speed`/
  `Heading` wrappers (see [`include/types.hpp`](../lib/LightNMEA/include/types.hpp))
- **Supported NMEA messages**: RMC, GGA, VTG, GLL
- **Constellation & fix-mode tracking**: GPS, GLONASS, Galileo, BeiDou, QZSS,
  NavIC, Multi + autonomous/DGPS/RTK/estimated/precise fix modes
- **Precise timing**: exactly one CSV line per second, no duplicates
  (deduplicated by UTC time)
- **Clean output**: 12-field CSV, ready for logging, analysis and the
  [`gnss_dashboard`](../lib/LightNMEA/examples/gnss_dashboard.cpp) TUI
- **LED indication**: blink on each valid GNSS fix + 500 ms heartbeat

## CSV Output Format

One line per second (only when a valid position is available). The field
order and formats follow the **canonical 12-field contract** of the PC-side
tools ([`LOG_CSV_HEADER`](../lib/LightNMEA/examples/dash_utils.hpp) /
[`GNSSData::from_csv()`](../lib/LightNMEA/examples/dash_utils.hpp)):

```
valid,satellites,latitude,longitude,speed,course,altitude,time,date,constellation,fix_mode,hdop
```

| # | Field          | Example        | Notes                                                      |
|---|----------------|----------------|------------------------------------------------------------|
| 1 | Valid          | `1`            | 0 / 1                                                      |
| 2 | Satellites     | `11`           | from GGA                                                   |
| 3 | Latitude       | `55.752220`    | decimal degrees                                            |
| 4 | Longitude      | `37.615555`    | decimal degrees                                            |
| 5 | Speed          | `23.4`         | km/h (converted from knots)                                |
| 6 | Course         | `84.4`         | true course, degrees                                       |
| 7 | Altitude       | `161.2`        | meters above MSL (from GGA)                                |
| 8 | Time (UTC)     | `19:27:13`     | `HH:MM:SS` (from RMC/GGA)                                  |
| 9 | Date (UTC)     | `16.07.2026`   | `DD.MM.20YY` (from RMC)                                    |
| 10 | Constellation | `GPS`          | name: GPS, GLONASS, Galileo, BeiDou, QZSS, NavIC, Multi-GNSS |
| 11 | Fix mode       | `Autonomous`   | name: Autonomous, DGPS, Estimated, Not Valid, RTK Fixed, RTK Float |
| 12 | HDOP           | `0.84`         | horizontal dilution of precision                           |

Example line:

```csv
1,11,55.752220,37.615555,23.4,84.4,161.2,19:27:13,16.07.2026,GPS,Autonomous,0.84
```

This stream is consumed as-is by the
[`gnss_dashboard`](../lib/LightNMEA/examples/gnss_dashboard.cpp) TUI and the
[`nmea_pc_logger.py`](nmea_pc_logger.py) PC logger (which writes
`timestamp,<this line>` rows into `gnss_log.csv`).

## How It Works

1. `loop()` reads bytes from the GNSS port into a 512-byte line buffer.
2. On `\n` / `\r` the accumulated line is handed to
   [`light_nmea::Parser::parse_line()`](../lib/LightNMEA/include/light_nmea.hpp:410).
3. After a successful parse (CRC-validated, constellation-filtered), the
   merged `NMEAData` (2D from RMC/GLL, 3D from GGA, motion from RMC/VTG) is
   checked with `has_coordinates()` / `valid`.
4. If the UTC time differs from the previously printed one, a CSV line is
   emitted and the LED blinks.

## Build & Flash

PlatformIO with the `pico` environment ([`platformio.ini`](../platformio.ini)):

```console
pio run -e pico          # build
pio run -e pico -t upload
pio device monitor -b 115200   # watch the CSV stream
```

For the Arduino UNO R3 with the Multi-Function Shield use `env:uno`:

```console
pio run -e uno -t upload
pio device monitor -b 115200
```

The same sources compile for the other supported boards once an
`[env:<board>]` section is added to `platformio.ini`.

## MultiFunction Shield HW-262 V0.0.5 (Arduino UNO R3)

The `env:uno` environment defines `LIGHTNMEA_USE_MFSHIELD`, which enables the
4-digit display driver in
[`lib/MFShieldDisplay`](../lib/MFShieldDisplay/src/mfshield_display.hpp).
A standalone verification sketch lives in
[`lib/LightNMEA/examples/uno_mfshield_display`](../lib/LightNMEA/examples/uno_mfshield_display/src/main.cpp):

```console
cd lib/LightNMEA/examples/uno_mfshield_display
pio run -e uno -t upload
pio device monitor -b 115200
```

Shield pin usage (HW-262, V0.0.5):

| Function | Pins |
|----------|------|
| 4-digit display (2 x 74HC595) | LATCH `D4`, CLOCK `D7`, DATA `D8` |
| LEDs D1-D4 (active LOW) | `D10`..`D13` |
| Buzzer (active LOW, PNP) | `D3` |
| IR receiver | `D2` |
| Potentiometer | `A0` |
| Buttons S1-S3 (pressed = LOW) | `A1`..`A3` |
| Temperature sensor (LM35 / DS18B20) | `A4` |
| Free for GNSS / external modules | `D5`, `D6`, `D9`, `A5` |

Because `D4` is the display latch, the GNSS SoftwareSerial port on UNO is moved
to `D5` (RX) / `D6` (TX). The display shows `HH.MM` (UTC) once a valid time
arrives from the GNSS module; until then it cycles through all supported
glyphs as a self-test. `refresh()` must be called from `loop()` - the driver
uses software multiplexing and no timers/interrupts.

Supported glyphs: `0`-`9`, `A`, `C`, `E`, `F`, `H`, `L`, `P`, `U`, `-`, `_`,
space and the decimal point (merged into the previous digit). Unknown
characters are shown as a blank digit.

Driver notes:

- Classic shield: segments active LOW, digit select active HIGH
  (`0xF1`, `0xF2`, `0xF4`, `0xF8`). Some clones invert both polarities - use
  `setWiring(MFShieldDisplay::Wiring::InvertedClone)` if characters look
  garbled or the display stays dark.
- If the text appears mirrored (right-to-left), call `setReverseDigits(true)`.
- Non-standard shield revisions with different 74HC595 pins can be handled by
  the constructor: `MFShieldDisplay(latch, clock, data)`.
- Software multiplexing means `refresh()` should be called on every `loop()`
  iteration; a full 4-digit cycle is ~5 ms.

### Native (host) build - no hardware needed

[`build_fw.sh`](../build_fw.sh) is a universal build script: it can compile the
same `main.cpp` for **x86** using an Arduino shim
([`native/Arduino.h`](../native/Arduino.h)) that maps:

- `Serial` (debug output) -> stdout
- `Serial1` (GNSS input)   -> stdin

This lets you validate the parsing/CSV logic on the host by replaying NMEA logs:

```console
./build_fw.sh                                    # build ./build-native/lightnmea_logger
cat track.nmea | ./build_fw.sh run --csv-only    # replay a .nmea log (CSV only)
./build_fw.sh run track.nmea --csv-only > out.csv
printf '$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\n' \
    | ./build_fw.sh run --csv-only

# Pico firmware targets delegate to build_pico.sh:
./build_fw.sh pico                                # PlatformIO build (env:pico)
./build_fw.sh upload                              # build + flash Pico
./build_fw.sh monitor                             # serial monitor
./build_fw.sh clean                               # clean native + PlatformIO caches
```

Compiler/flags can be overridden via the `CXX` and `CXXFLAGS` environment
variables.

## Wiring

Connect the GNSS module's TX to the board's RX pin (see table above), the
module's RX to the board's TX pin, and power/GND as usual. Default GNSS baud
rate is **38400** (`GPS_BAUD_RATE` in `main.cpp`) - adjust if your module
uses a different rate (e.g. 9600).
