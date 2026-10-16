/*
 * LightNMEA - Ultra-fast NMEA GPS parser
 * Copyright (C) 2026 Roman Shevchik
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file lightnmea_c.cpp
 * @brief Реализация C API (см. include/lightnmea_c.h).
 *
 * Файл лежит в src/, поэтому компилируется автоматически:
 *   - Arduino IDE: все .cpp из src/;
 *   - PlatformIO: все .cpp из src/ папки библиотеки;
 *   - CMake: цель light_nmea_c (см. CMakeLists.txt).
 */

#include "../include/lightnmea_c.h"

#include "light_nmea.hpp"

#include <new>
#include <string_view>

// Определение типа: хранит C++-парсер.
struct lightnmea_parser {
  light_nmea::Parser impl;
};

extern "C" {

lightnmea_parser_t *lightnmea_create(void) {
  return new (std::nothrow) lightnmea_parser_t{};
}

void lightnmea_destroy(lightnmea_parser_t *p) {
  delete p;
}

int lightnmea_parse_line(lightnmea_parser_t *p, const char *line) {
  if (p == nullptr || line == nullptr) return 0;
  return p->impl.parse_line(std::string_view(line)) ? 1 : 0;
}

int lightnmea_parse_line_n(lightnmea_parser_t *p, const char *line, size_t len) {
  if (p == nullptr || line == nullptr) return 0;
  return p->impl.parse_line(std::string_view(line, len)) ? 1 : 0;
}

void lightnmea_get_data(const lightnmea_parser_t *p, lightnmea_data_t *out) {
  if (p == nullptr || out == nullptr) return;
  const light_nmea::NMEAData &d = p->impl.data();
  out->lat_raw = d.lat.raw;
  out->lon_raw = d.lon.raw;
  out->speed_kn_x1000 = d.speed_kn_x1000.raw;
  out->speed_kmh_x1000 = d.speed_kmh_x1000.raw;
  out->course_true_e2 = d.course_true.raw;
  out->course_mag_e2 = d.course_mag.raw;
  out->altitude_cm = d.altitude_cm;
  out->hdop_e2 = d.hdop_e2;
  out->satellites = d.satellites;
  out->valid = d.valid ? 1u : 0u;
  out->time_utc = d.time_utc;
  out->date = d.date;
  out->constellation = static_cast<uint8_t>(d.constellation);
  out->fix_mode = static_cast<uint8_t>(d.fix_mode);
  out->msg_type = static_cast<uint8_t>(d.msg_type);
  out->has_lat_lon = d.has_lat_lon ? 1u : 0u;
  out->has_speed_course = d.has_speed_course ? 1u : 0u;
  out->has_altitude = d.has_altitude ? 1u : 0u;
  out->has_vtg_speed = d.has_vtg_speed ? 1u : 0u;
  out->has_vtg_course = d.has_vtg_course ? 1u : 0u;
}

void lightnmea_set_cst_filter(lightnmea_parser_t *p, uint32_t mask) {
  if (p != nullptr) p->impl.set_cst_filter(mask);
}

} // extern "C"