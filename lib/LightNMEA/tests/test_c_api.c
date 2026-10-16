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
 * @file test_c_api.c
 * @brief Unit-тест C API (чистый Си, без C++).
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lightnmea_c.h"

int main(void) {
  lightnmea_parser_t *p = lightnmea_create();
  assert(p != NULL);

  /* RMC: Мюнхен (48.1173 N, 11.516667 E, 22.4 узла, курс 84.4) */
  assert(lightnmea_parse_line(
      p, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  lightnmea_data_t d;
  lightnmea_get_data(p, &d);
  assert(d.valid == 1);
  assert(d.has_lat_lon == 1);
  assert(d.lat_raw == 481173000);      /* 48.1173 * 1e7 */
  assert(d.lon_raw == 115166670);      /* 11.516667 * 1e7 */
  assert(d.speed_kn_x1000 == 22400);   /* 22.4 * 1000 */
  assert(d.course_true_e2 == 8440);    /* 84.40 * 100 */
  assert(d.msg_type == LIGHTNMEA_MSG_RMC);
  assert(d.constellation == LIGHTNMEA_CST_GPS);

  /*  GGA: высота 545.4 м, HDOP 0.9, 8 спутников  */
  assert(lightnmea_parse_line(
      p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
  lightnmea_get_data(p, &d);
  assert(d.has_altitude == 1);
  assert(d.altitude_cm == 54540);      /* 545.4 м */
  assert(d.satellites == 8);
  assert(d.hdop_e2 == 90);             /* 0.9 * 100 */
  assert(d.msg_type == LIGHTNMEA_MSG_GGA);

  /*  parse_line_n (без NUL)  */
  const char *line =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
  assert(lightnmea_parse_line_n(p, line, strlen(line)));

  /*  Мусор не парсится  */
  assert(!lightnmea_parse_line(p, "garbage"));
  assert(!lightnmea_parse_line(p, NULL));
  assert(!lightnmea_parse_line(NULL, line));

  /*  NULL-safe destroy  */
  lightnmea_destroy(p);
  lightnmea_destroy(NULL);

  printf("C API tests passed\n");
  return 0;
}