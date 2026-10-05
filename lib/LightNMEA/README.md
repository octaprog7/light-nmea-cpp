# light-nmea-cpp

Ultra-fast NMEA 0183 GPS parser for embedded systems and Arduino.

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
2. Sketch → Include Library → Add .ZIP Library
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

## Examples
* BasicGPS: Simple GPS data display
* GPSToJSON: Parse and output as JSON
* GPSLogger: Log GPS data to SD card

## Performance
|Platform|Speed|Flash|RAM|
|x86_64|11+ M pkt/s|-|-|
|Cortex-M0+|5-10x faster than float|~1.3 KB|52 byte|
|ESP32|11+ M pkt/s|~1.3 KB|52 byte|

# License
GPL v3.0