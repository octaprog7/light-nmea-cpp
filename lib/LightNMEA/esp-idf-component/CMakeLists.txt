# ============================================================================
# LightNMEA - ESP-IDF component
# ============================================================================
# Компонент для ESP-IDF (не Arduino). Подключает заголовки библиотеки и компилирует C API (src/lightnmea_c.cpp).
#
# Использование:
#   1. Скопируйте эту папку в components/ вашего проекта ESP-IDF
#      (или укажите EXTRA_COMPONENT_DIRS в CMakeLists проекта);
#   2. В коде: #include "light_nmea.hpp" (C++) или "lightnmea_c.h" (C);
#   3. Добавьте REQUIRES lightnmea в idf_component_register вашего компонента.
# ============================================================================

idf_component_register(
    SRCS "../src/lightnmea_c.cpp"
    INCLUDE_DIRS "../include"
)