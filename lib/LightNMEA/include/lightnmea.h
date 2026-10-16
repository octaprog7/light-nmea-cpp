/**
 * @file lightnmea.h
 * @brief Arduino-compatible header for LightNMEA library
 *
 * Примечание: в src/lightnmea.h лежит forwarding-обёртка для Arduino IDE
 * (Library 1.5 добавляет в include path только src/). Это не дубликат -
 * обёртка просто реэкспортирует этот заголовок.
 */
#pragma once

#include "light_nmea.hpp"
#include "nmea_formatter.hpp"

// Алиас/псевдоним namespace для удобства: `lightnmea::Parser` == `light_nmea::Parser`.
// Используется в примерах (BasicGPS/GPSLogger/GPSToJSON) и в README.
namespace lightnmea = light_nmea;
