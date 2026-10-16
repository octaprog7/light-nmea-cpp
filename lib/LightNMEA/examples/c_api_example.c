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
 * @file c_api_example.c
 * @brief Пример использования C API LightNMEA из проекта на чистом C.
 *
 * Сборка (CMake, из каталога lib/LightNMEA):
 *   cmake -S . -B build && cmake --build build
 *   ./build/c_api_example
 */

#include <stdio.h>

#include "lightnmea_c.h"

int main(void) {
  lightnmea_parser_t *parser = lightnmea_create();
  if (parser == NULL) {
    fprintf(stderr, "Cannot allocate parser\n");
    return 1;
  }

  /* RMC: Мюнхен, 48.07038 N, 11.31000 E, 22.4 узла, курс 84.4 */
  const char *rmc =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

  if (lightnmea_parse_line(parser, rmc)) {
    lightnmea_data_t d;
    lightnmea_get_data(parser, &d);

    printf("Parsed RMC:\n");
    printf("  valid:        %u\n", (unsigned)d.valid);
    printf("  lat:          %d (1e-7 deg) = %.7f\n",
           d.lat_raw, (double)d.lat_raw / LIGHTNMEA_SCALE_COORD);
    printf("  lon:          %d (1e-7 deg) = %.7f\n",
           d.lon_raw, (double)d.lon_raw / LIGHTNMEA_SCALE_COORD);
    printf("  speed:        %d (x1000 kn) = %.3f kn\n",
           d.speed_kn_x1000, (double)d.speed_kn_x1000 / LIGHTNMEA_SCALE_SPEED);
    printf("  course:       %d (1e-2 deg) = %.2f deg\n",
           d.course_true_e2, (double)d.course_true_e2 / LIGHTNMEA_SCALE_HEADING);
    printf("  satellites:   %u\n", (unsigned)d.satellites);
    printf("  constellation:%u\n", (unsigned)d.constellation);
    printf("  fix_mode:     %u\n", (unsigned)d.fix_mode);
    printf("  msg_type:     %u (RMC=%d)\n",
           (unsigned)d.msg_type, (int)LIGHTNMEA_MSG_RMC);
  } else {
    printf("RMC not parsed\n");
  }

  /* GGA: высота и HDOP */
  const char *gga =
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
  if (lightnmea_parse_line(parser, gga)) {
    lightnmea_data_t d;
    lightnmea_get_data(parser, &d);
    printf("Parsed GGA: altitude=%d cm, hdop=%d (e2), sats=%u\n",
           d.altitude_cm, d.hdop_e2, (unsigned)d.satellites);
  }

  /* Фильтр созвездий: только GPS */
  lightnmea_set_cst_filter(parser, LIGHTNMEA_CST_MASK_GPS);

  lightnmea_destroy(parser);
  return 0;
}