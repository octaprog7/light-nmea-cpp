# LightNMEA GPS Logger - Main Firmware

## Overview

`main.cpp` is the main firmware for Raspberry Pi Pico that:
- Receives NMEA sentences from GPS module via UART (Serial1)
- Parses GPS data using high-performance LightNMEA library
- Outputs clean CSV-formatted data to USB serial (Serial) at 115200 baud

##  Features

- **High-performance parsing**: Integer-only math, zero heap allocations
- **Precise timing**: Outputs exactly one CSV line per second (no duplicates)
- **Clean output**: CSV format ready for logging and analysis
- **Minimal resources**: Uses only ~3.5% of RAM and Flash
