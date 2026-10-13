/**
 * @file lightnmea.h
 * @brief Arduino-compatible header for LightNMEA library
 */
#pragma once

#include "light_nmea.hpp"
#include "nmea_formatter.hpp"

// Алиас namespace для удобства: `lightnmea::Parser` == `light_nmea::Parser`.
// Используется в примерах (BasicGPS/GPSLogger/GPSToJSON) и в README.
namespace lightnmea = light_nmea;
