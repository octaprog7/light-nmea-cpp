# LightNMEA + RT-Thread

Example of integrating the LightNMEA library with [RT-Thread](https://www.rt-thread.io/)
RTOS. The application reads an NMEA data stream from a UART device
(`rt_device` + `rx_indicate` callback + semaphore), accumulates the data in a
ring buffer, and parses it in the main thread with the LightNMEA library
(`NMEAStreamReader` + `Parser`).

The LightNMEA core (`light_nmea.hpp`, `nmea_stream_reader.hpp`,
`nmea_formatter.hpp`) does not depend on Arduino/POSIX, so it runs in
RT-Thread unchanged.

## Two modes

| Mode | Data source | Selected by |
|------|-------------|-------------|
| UART (default) | real GPS module on a serial device | — |
| Simulation | built-in sample sentences, producer thread | `LIGHTNMEA_SIM_FEED=y` |

The simulation mode is handy for boards without a GPS attached
(e.g. `qemu-vexpress-a9`) or CI smoke tests.

## Requirements

- RT-Thread 4.x BSP with:
  - C++17 support (`RT_USING_CPLUSPLUS`, `gnu++17` in `CXXFLAGS`);
  - serial device driver (`RT_USING_SERIAL`, `RT_USING_SERIAL_V2` also works)
    for the UART mode.

## Build & run

### Real board (e.g. STM32 BSP)

1. Copy this directory into the BSP tree (or mount it as the `applications`
   directory) so that `SConscript` is picked up by SCons.
2. Enable the example in `menuconfig` (`Kconfig` from this directory):
   - `LIGHTNMEA_UART_NAME` — name of the serial device connected to the GPS
     (default `uart1`).
3. Build and flash:

   ```console
   scons
   ```

4. Connect the GPS module to the selected UART (9600 baud for most modules)
   and watch the RT-Thread console (msh):

   ```
   LightNMEA RT-Thread example started (UART: uart1)
   Fix: Yes
   satellites: 8
   latitude: 48.117300
   longitude: 11.516667
   ...
   ```

### QEMU (qemu-vexpress-a9, no hardware)

```console
# in the qemu-vexpress-a9 BSP directory with this example added:
menuconfig            # LightNMEA example -> enable LIGHTNMEA_SIM_FEED
scons
./qemu.sh
```

The built-in producer thread feeds sample NMEA sentences into the ring
buffer and the parsed positions are printed to the console.

## How it works

1. `rt_device_find(LIGHTNMEA_UART_NAME)` + `rt_device_open(..., INT_RX)` —
   the serial device is opened in interrupt RX mode.
2. The `rx_indicate` callback (`uart_input`) only releases the `ln_rx`
   semaphore — no parsing in interrupt context.
3. The main thread wakes on the semaphore, drains the driver buffer into the
   ring buffer (`uart_drain`).
4. `RingStream` implements the abstract `light_nmea::Stream::read()` over the
   ring buffer.
5. `reader.read_available(parser, stream, get_time_ms)` assembles NMEA
   packets and feeds them to the parser (`rt_tick_get_millisecond()` provides
   the anti-spam timestamps).
6. the result is printed with `rt_kprintf`.

## Notes

- The ring buffer (512 B) absorbs bursts between scheduler ticks; bytes are
  silently dropped on overflow (GPS keeps streaming, the next sentences are
  still valid).
- In the simulation mode a producer thread (`ln_sim`) fills the same ring
  buffer, so the parsing path is identical to the UART mode.
- `main()` runs in the standard RT-Thread main thread; no extra threads are
  created in the UART mode.
