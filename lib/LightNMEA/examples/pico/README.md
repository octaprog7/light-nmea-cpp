# LightNMEA + Raspberry Pi Pico (RP2040)

Example for the Raspberry Pi Pico (RP2040, Cortex-M0+ @ 133 MHz) with a
**G10F-F30** GNSS module wired to **GP0/GP1 (UART0)**:

| Pico pin | Signal | GNSS module |
|----------|--------|-------------|
| GP0 (UART0 TX) | commands (optional) | RX |
| GP1 (UART0 RX) | NMEA-0183 stream | TX |
| 3V3 / GND | power | VCC / GND |

**No GPS signal?** Indoors there is no fix, and without a module at all the
UART stays silent. For such cases the firmware has a **demo mode**
(`[env:pico_demo]`) that serves a built-in synthetic NMEA track through the
exact same `Stream -> NMEAStreamReader -> Parser` pipeline — no module and no
satellites needed.

Parsed data is printed to **USB CDC** (the "Pico" serial port on the host),
so UART0 stays fully dedicated to the GNSS module. The on-board LED (GP25)
toggles on every received packet and goes dark if no data arrives for 5 s.

## Requirements

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/) 6.x
  (the `raspberrypi` platform and the earlephilhower Arduino core are
  downloaded automatically on first build).

## Build & flash

```console
cd lib/LightNMEA/examples/pico
pio run -e pico                # build (real mode: GNSS on UART0)
pio run -e pico -t upload      # flash (picotool reboots the board to BOOTSEL
                               #  automatically — no button needed)
pio run -e pico_demo -t upload # demo mode: built-in NMEA track, module not used
```

If `picotool` fails to take over the running application, hold the **BOOTSEL**
button while plugging the board in and re-run the upload.

## Monitor

```console
pio device monitor             # 115200, USB CDC
```

After each reset the firmware prints a banner (it waits up to 4 s for the
terminal to connect), then continuously prints recognized positions:

```
=== LightNMEA Pico example (G10F-F30 on GP0/GP1) ===
GNSS UART0: 9600 baud, TX GP0 / RX GP1
Fix: Yes
satellites: 8
latitude: 48.117300
longitude: 11.516667
...
```

If you see `no GNSS data for 5 s (check wiring / baud rate)`, check:

- wiring: module TX must go to **GP1**, not GP0;
- baud rate: G10F-F30 default is **38400** (already set in `platformio.ini`);
  override with `pio run -e pico -DGNSS_BAUD=9600` (or 115200) if your module
  is configured differently;
- power: the module needs a clean 3.3 V and may take several seconds to a
  minute for the first fix.

## Configuration (build flags)

| Flag | Default | Meaning |
|------|---------|---------|
| `GNSS_BAUD` | 38400 | UART0 baud rate of the GNSS module |
| `GNSS_TX_PIN` | 0 | GP pin used as UART0 TX (to module RX) |
| `GNSS_RX_PIN` | 1 | GP pin used as UART0 RX (from module TX) |
| `LED_PIN` | 25 | activity LED |

## How it works

1. `Serial1` (UART0) receives the NMEA-0183 stream from the module.
2. `GnssStream` implements the abstract `light_nmea::Stream::read()` over
   `Serial1` (non-blocking, drains whatever is buffered).
3. `reader.read_available(parser, stream, get_time_ms)` assembles NMEA
   sentences (with the anti-spam filter driven by `millis()`) and feeds them
   to the `light_nmea::Parser`.
4. `light_nmea::to_txt()` formats the fix; `Serial` (USB CDC) prints it.

Note: Arduino defines its own `Stream` class, so the example always qualifies
LightNMEA types (`light_nmea::Parser`, `light_nmea::Stream`, ...) instead of
using `using namespace light_nmea`.
