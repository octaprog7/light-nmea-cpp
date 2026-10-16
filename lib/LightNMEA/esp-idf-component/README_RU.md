# LightNMEA ESP-IDF component

Компонент для использования LightNMEA в проектах **ESP-IDF** (без Arduino).

## Подключение

Скопируйте папку `esp-idf-component` в `components/lightnmea` вашего проекта
(или добавьте путь в `EXTRA_COMPONENT_DIRS` в корневом `CMakeLists.txt`):

```cmake
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components/lightnmea)
```

В `CMakeLists.txt` вашего компонента укажите зависимость:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES lightnmea
)
```

## Использование

C++:

```cpp
#include "light_nmea.hpp"

light_nmea::Parser parser;
if (parser.parse_line("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A")) {
    // parser.data().lat.raw ...
}
```

C (чистый C):

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

Компонент компилирует `src/lightnmea_c.cpp` (C API) и добавляет `include/`
библиотеки в include path. Ядро библиотеки остаётся header-only.