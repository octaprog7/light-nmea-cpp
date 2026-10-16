# LightNMEA ESP-IDF component

An ESP-IDF component for using LightNMEA in **ESP-IDF** projects (without Arduino).

## Integration

Copy the `esp-idf-component` folder to `components/lightnmea` in your project
(or add the path to `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`):

```cmake
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components/lightnmea)
```

Declare the dependency in your component's `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES lightnmea
)
```

## Usage

C++:

```cpp
#include "light_nmea.hpp"

light_nmea::Parser parser;
if (parser.parse_line("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A")) {
    // parser.data().lat.raw ...
}
```

C (plain C):

```c
#include "lightnmea_c.h"

lightnmea_parser_t *p = lightnmea_create();
if (lightnmea_parse_line(p, "$GPRMC,...")) {
    lightnmea_data_t d;
    lightnmea_get_data(p, &d);
    /* d.lat_raw, d.lon_raw ... */
}
lightnmea_destroy(p);
```

The component compiles `src/lightnmea_c.cpp` (C API) and adds the library's
`include/` directory to the include path. The library core remains header-only.