# LightNMEA RTOS benchmarks (FreeRTOS & Zephyr)

Comparative NMEA-parser benchmark for RTOSes: **LightNMEA** vs **minmea** vs
**TinyGPSPlus** on identical packets from `nav_gen::NAV_PACKETS` (RMC/GGA/VTG/GLL
from GP/GN talkers). This is an RTOS adaptation of
[`benchmarks/firmware/src/bench_compare_mcu.cpp`](../firmware/src/bench_compare_mcu.cpp): the hot loops and measurement logic are ported unchanged; only the Arduino HAL (`micros()`, `Serial`) is replaced with RTOS APIs.

## Layout

```
rtos/
├── bench_compare_rtos.hpp   # shared portable core (hot loops, best-of-N,
│                            #   metrics). RTOS-independent: the timer and
│                            #   output are injected via BenchEnv.
├── Arduino.h                # minimal Arduino shim to build TinyGPSPlus
│                            #   (millis() is defined in the RTOS shim).
├── freertos/                # FreeRTOS shim (POSIX port, host)
├── zephyr/                  # Zephyr shim (native_sim / nRF52840 DK)
└── esp32/                   # ESP32 D1 mini (FreeRTOS, ESP-IDF/Arduino)
```

The competitors (minmea, TinyGPSPlus) are taken from the vendored copies in
[`benchmarks/firmware/lib`](../firmware/lib), kept in sync with
[`benchmarks/compare/third_party`](../compare/third_party). In `esp32/` they
live directly in `src/` (PlatformIO compiles everything under `src/`
unconditionally, and LDF does not track includes by relative path to the shared core).

## Metrics

- **pkt/s** — throughput (packets per second);
- **ns/pkt** — nanoseconds per packet;
- **cycles/pkt** — cycles per packet (from `F_CPU`);
- **pkt/s/MHz** — pkt/s per 1 MHz of clock frequency;
- **ok/total** — fraction of successfully parsed packets.

Best-of-N runs (3 by default). Configuration via build flags:
`BENCH_PACKET_COUNT` (default 30000) and `BENCH_PASSES` (default 3).

## FreeRTOS (host, POSIX port)

```console
cd lib/LightNMEA/benchmarks/rtos/freertos
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/freertos_nmea_bench
```

Example output:

```
=== LightNMEA RTOS comparative benchmark ===
Packets/pass: 30000, passes: 3 (best), F_CPU: 100000000 Hz

Implementation  pkt/s       ns/pkt  cycles/pkt  pkt/s/MHz  ok/total
LightNMEA         7816571      127         13      78165  30000/30000
minmea            2989238      334         33      29892  30000/30000
TinyGPSPlus       2705627      369         37      27056  30000/30000
```

## Zephyr

```console
cd lib/LightNMEA/benchmarks/rtos/zephyr
west build -b native_sim -d build/native_sim
./build/native_sim/zephyr/zephyr.exe
```

Real board (tested: nRF52840 DK):

```console
west build -b nrf52840dk/nrf52840 -d build/nrf52840dk
west flash -d build/nrf52840dk
```

`native_sim` requires 32-bit host libraries:
`sudo apt install gcc-multilib g++-multilib` (same as for the
[`examples/zephyr`](../../examples/zephyr/README.md) example).

## ESP32 D1 mini (FreeRTOS via ESP-IDF/Arduino)

```console
cd lib/LightNMEA/benchmarks/rtos/esp32
pio run -t upload -e d1_mini32
pio device monitor -b 115200
```

The benchmark runs in a FreeRTOS task (`xTaskCreate`, priority 2 — above
loopTask, so the measurement is not interrupted), timing via
`esp_timer_get_time()` (hardware timer), output via `printf` (redirected to
Serial UART0). Task WDT is disabled (`disableCore0WDT()`/`disableCore1WDT()`):
the benchmark is a long-running CPU-bound task, otherwise the IDLE task cannot
feed the watchdog (5 s by default).

## How it works

1. The RTOS shim creates a `bench_rtos::BenchEnv` with two functions:
   - `now_us()` — microsecond time source;
   - `print()` — printf-compatible output.
2. `bench_rtos::run_benchmark()` runs the three parsers on the same packet set
   (best-of-N) and prints the table.
3. The hot loops do not use the heap or FPU — they run on Cortex-M0+ and RISC-V.

Time sources:

| RTOS | now_us() | Notes |
|---|---|---|
| FreeRTOS (POSIX port) | `clock_gettime(CLOCK_MONOTONIC)` | microsecond resolution |
| Zephyr | `k_cyc_to_us_ceil32(k_cycle_get_32())` | hardware cycle counter |

## Porting to other hardware

- **FreeRTOS on MCU:** replace `now_us()` with the platform cycle counter
  (Cortex-M: `DWT->CYCCNT`; ESP32: `esp_timer_get_time()`; RISC-V: `mcycle`),
  and provide your own `FreeRTOSConfig.h` and port instead of POSIX.
- **Zephyr:** `k_cycle_get_32()` already uses the hardware cycle counter.
  `F_CPU` defaults to `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC`; for the nRF52840
  (system timer — RTC 32.768 kHz) `F_CPU=64000000` (CPU frequency) is set in
  `CMakeLists.txt` so that the cycles/pkt metric is meaningful. For another
  board, set your own `F_CPU` via `target_compile_definitions`.

## Comparison with the firmware benchmark

[`benchmarks/firmware`](../firmware) runs the same measurement, but under
Arduino (PlatformIO) on Pico/ESP32/STM32, etc. The RTOS version differs only in
the time source and output; the metrics and hot loops are identical, so the
results are comparable.

### Measurement on ESP32 D1 mini (240 MHz, 30000 packets, best-of-3)

| Parser | RTOS (FreeRTOS) pkt/s | firmware (Arduino) pkt/s | Diff |
|---|---|---|---|
| LightNMEA | 110 897 | 110 629 | +0.24% |
| minmea | 34 000 | 33 955 | +0.13% |
| TinyGPSPlus | 34 810 | 33 882 | +2.7% |

The results match within a small margin: the FreeRTOS task context and
`esp_timer_get_time()` add no(!) measurable overhead to the hot loop. All
30000/30000 packets were parsed in both cases.