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
 * @file lightnmea_c.h
 * @brief C API для LightNMEA (обёртка над ядром C++).
 *
 * Позволяет использовать парсер из проектов на Си.
 * Реализация - src/lightnmea_c.cpp (компилируется Arduino IDE / PlatformIO / CMake автоматически).
 *
 * Все значения - целочисленные raw-поля (без float/double), масштабы совпадают
 * с C++-версией (light_nmea::scale):
 *   - координаты: 1e-7 градуса (LIGHTNMEA_SCALE_COORD);
 *   - курс:       1e-2 градуса (LIGHTNMEA_SCALE_HEADING);
 *   - скорость:   x1000 (LIGHTNMEA_SCALE_SPEED).
 *
 * Пример использования см. в examples/c_api_example.c.
 */

#ifndef LIGHT_NMEA_C_H
#define LIGHT_NMEA_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Масштабы (совпадают с light_nmea::scale)
 * ------------------------------------------------------------------------*/
#define LIGHTNMEA_SCALE_COORD   10000000 /* 1e-7 градуса (~1.1 см) */
#define LIGHTNMEA_SCALE_HEADING 100      /* 1e-2 градуса */
#define LIGHTNMEA_SCALE_SPEED   1000     /* x1000 (узлы/км/ч) */

/* --------------------------------------------------------------------------
 * Перечисления (совпадают с light_nmea::Constellation / FixMode / MessageType)
 * ------------------------------------------------------------------------*/
enum lightnmea_constellation {
  LIGHTNMEA_CST_UNKNOWN = 0,
  LIGHTNMEA_CST_GPS,
  LIGHTNMEA_CST_GLONASS,
  LIGHTNMEA_CST_GALILEO,
  LIGHTNMEA_CST_BEIDOU,
  LIGHTNMEA_CST_QZSS,
  LIGHTNMEA_CST_NAVIC,
  LIGHTNMEA_CST_MULTI
};

enum lightnmea_fix_mode {
  LIGHTNMEA_FIX_AUTONOMOUS = 0,
  LIGHTNMEA_FIX_DGPS,
  LIGHTNMEA_FIX_ESTIMATED,
  LIGHTNMEA_FIX_NOT_VALID,
  LIGHTNMEA_FIX_RTK_FIXED,
  LIGHTNMEA_FIX_RTK_FLOAT,
  LIGHTNMEA_FIX_PPS,
  LIGHTNMEA_FIX_MANUAL_INPUT,
  LIGHTNMEA_FIX_SIMULATOR,
  LIGHTNMEA_FIX_PRECISE
};

enum lightnmea_msg_type {
  LIGHTNMEA_MSG_UNKNOWN = 0,
  LIGHTNMEA_MSG_RMC,
  LIGHTNMEA_MSG_GGA,
  LIGHTNMEA_MSG_VTG,
  LIGHTNMEA_MSG_GLL
};

/* --------------------------------------------------------------------------
 * Маски фильтра созвездий (совпадают с CST_MASK_* из light_nmea.hpp)
 * ------------------------------------------------------------------------*/
#define LIGHTNMEA_CST_MASK_GPS     (1u << LIGHTNMEA_CST_GPS)
#define LIGHTNMEA_CST_MASK_GLONASS (1u << LIGHTNMEA_CST_GLONASS)
#define LIGHTNMEA_CST_MASK_GALILEO (1u << LIGHTNMEA_CST_GALILEO)
#define LIGHTNMEA_CST_MASK_BEIDOU  (1u << LIGHTNMEA_CST_BEIDOU)
#define LIGHTNMEA_CST_MASK_QZSS    (1u << LIGHTNMEA_CST_QZSS)
#define LIGHTNMEA_CST_MASK_NAVIC   (1u << LIGHTNMEA_CST_NAVIC)
#define LIGHTNMEA_CST_MASK_MULTI   (1u << LIGHTNMEA_CST_MULTI)
#define LIGHTNMEA_CST_MASK_ALL     0xFFFFFFFFu

/* --------------------------------------------------------------------------
 * Распарсенные данные (плоская C-структура, целочисленные raw-поля)
 * ------------------------------------------------------------------------*/
typedef struct lightnmea_data {
  int32_t lat_raw;           /* 1e-7 градуса */
  int32_t lon_raw;           /* 1e-7 градуса */
  int32_t speed_kn_x1000;    /* тысячные доли узла */
  int32_t speed_kmh_x1000;   /* тысячные доли км/ч (из VTG, поле K) */
  int32_t course_true_e2;    /* 1e-2 градуса */
  int32_t course_mag_e2;     /* 1e-2 градуса */
  int32_t altitude_cm;       /* сантиметры */
  int16_t hdop_e2;           /* HDOP x100 */
  uint8_t satellites;        /* число спутников */
  uint8_t valid;             /* 1 - данные валидны */
  uint32_t time_utc;         /* время UTC (HHMMSS) */
  uint32_t date;             /* дата (DDMMYY) */
  uint8_t constellation;     /* enum lightnmea_constellation */
  uint8_t fix_mode;          /* enum lightnmea_fix_mode */
  uint8_t msg_type;          /* enum lightnmea_msg_type */
  uint8_t has_lat_lon;       /* 1 - есть координаты */
  uint8_t has_speed_course;  /* 1 - есть скорость и курс */
  uint8_t has_altitude;      /* 1 - есть высота */
  uint8_t has_vtg_speed;     /* 1 - скорость из VTG */
  uint8_t has_vtg_course;    /* 1 - курс из VTG */
} lightnmea_data_t;

/* Непрозрачный тип парсера (определение - в src/lightnmea_c.cpp) */
typedef struct lightnmea_parser lightnmea_parser_t;

/* --------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------*/

/** @brief Создать парсер. Возвращает NULL при нехватке памяти. */
lightnmea_parser_t *lightnmea_create(void);

/** @brief Уничтожить парсер (NULL-safe). */
void lightnmea_destroy(lightnmea_parser_t *p);

/**
 * @brief Разобрать одну NMEA-строку (NUL-terminated).
 * @return 1 - строка распознана и данные обновлены, 0 - иначе.
 */
int lightnmea_parse_line(lightnmea_parser_t *p, const char *line);

/**
 * @brief Разобрать строку с явной длиной (без NUL).
 * @return 1 - строка распознана и данные обновлены, 0 - иначе.
 */
int lightnmea_parse_line_n(lightnmea_parser_t *p, const char *line, size_t len);

/** @brief Скопировать последние распарсенные данные в out. */
void lightnmea_get_data(const lightnmea_parser_t *p, lightnmea_data_t *out);

/** @brief Установить фильтр созвездий (битовая маска LIGHTNMEA_CST_MASK_*). */
void lightnmea_set_cst_filter(lightnmea_parser_t *p, uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_NMEA_C_H */