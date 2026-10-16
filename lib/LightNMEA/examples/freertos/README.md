# LightNMEA + FreeRTOS (host, POSIX port)

Example of integrating the LightNMEA library with FreeRTOS on a Linux/macOS host
using the FreeRTOS kernel POSIX port. No hardware required.

## What it does

- **Producer task** reads an NMEA track from `data/sample.nmea` and
  sends the bytes to a FreeRTOS queue.
- **Consumer task** implements the abstract `light_nmea::Stream` over the
  queue (`QueueStream`) and parses the data with `NMEAStreamReader` + `Parser`.
- The LightNMEA core (`light_nmea.hpp`, `nmea_stream_reader.hpp`,
  `nmea_formatter.hpp`) does not depend on Arduino/POSIX, so it runs in any
  FreeRTOS task unchanged.

## Requirements

- CMake >= 3.16
- C/C++ compiler (GCC/Clang)
- Internet access on first setup (the FreeRTOS kernel is downloaded from GitHub)

## Build & run

```console
cd lib/LightNMEA/examples/freertos
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/freertos_nmea_example
```

Expected output (excerpt):

```
LightNMEA FreeRTOS example: starting scheduler
Producer: sent 342 bytes to queue
Fix: Yes
satellites: 8
latitude: 48.117300
longitude: 11.516667
...
Consumer: done. processed=6 aborted=0 anti_spam_dropped=0
```

## Porting to real hardware

1. Replace `QueueStream` with a `Stream` implementation over your UART driver
   (or keep the queue if the data stream comes from another task/ISR).
2. Use your own `FreeRTOSConfig.h` and the FreeRTOS port for your(!) platform
   instead of the POSIX port.
3. Replace `get_time_ms()` with a time function for your(!) platform, e.g.
   `xTaskGetTickCount()`.