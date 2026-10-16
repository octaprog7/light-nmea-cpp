# LightNMEA + Zephyr

Example of integrating the LightNMEA library with Zephyr RTOS. The application
reads an NMEA data stream from UART via the `uart_pipe` API, accumulates the
data in a ring buffer, and parses it in the main loop with the LightNMEA
library (`NMEAStreamReader` + `Parser`).

The LightNMEA core (`light_nmea.hpp`, `nmea_stream_reader.hpp`,
`nmea_formatter.hpp`) does not depend on Arduino/POSIX, so it runs in Zephyr
unchanged.

## Requirements

- [Zephyr RTOS](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
  (west + Zephyr SDK), version 3.6+.
- For `native_sim`, 32-bit host libraries are also required:
  `sudo apt install gcc-multilib g++-multilib`.

## Build & run

### native_sim (no hardware)

```console
cd lib/LightNMEA/examples/zephyr
west build -b native_sim -d build/native_sim
./build/native_sim/zephyr/zephyr.exe
```

On native_sim, UART0 is connected to stdin/stdout: type NMEA lines into the
terminal (e.g. `$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A`) and the application will print the recognized coordinates.

### Board (tested: nRF52840 DK)

```console
cd lib/LightNMEA/examples/zephyr
west build -b nrf52840dk/nrf52840 -d build/nrf52840dk
west flash -d build/nrf52840dk
```

The overlay for the nRF52840 DK is in `boards/nrf52840dk_nrf52840.overlay`
(UART0, pins P0.06 TX / P0.08 RX, 115200 baud). Connect a GPS module to
UART0 and watch the output with `west build -t monitor` (or your own terminal).

For another board, create your own overlay in `boards/` and set
`chosen { zephyr,uart-pipe = &uartX; };` to the desired UART.

## How it works

1. `uart_pipe_register(buf, len, pipe_handler)` registers a buffer and a
   handler that receives data from UART (the device is selected from the
   devicetree `chosen: zephyr,uart-pipe`).
2. `pipe_handler` writes bytes into the ring buffer.
3. `RingStream` implements the abstract `light_nmea::Stream::read()` over the
   ring buffer.
4. `reader.read_available(parser, stream, get_time_ms)` assembles NMEA packets
   and feeds them to the parser;
5. the result is printed with `printk`.

## Configuration (prj.conf)

- `CONFIG_UART_PIPE=y` — the uart_pipe API.
- `CONFIG_CPP=y`, `CONFIG_STD_CPP17=y`, `CONFIG_REQUIRES_FULL_LIBCPP=y` —
  C++17 and the full libstdc++ (required for `std::string_view::substr` and
  the C++ runtime).