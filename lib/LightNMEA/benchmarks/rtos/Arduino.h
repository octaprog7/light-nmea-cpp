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
 * @file Arduino.h
 * @brief Минимальная эмуляция среды Arduino для сборки TinyGPSPlus под ОСРВ.
 *
 * Аналог benchmarks/compare/Arduino.h, но millis() НЕ использует std::chrono
 * (на МК chrono-таймер может отсутствовать): millis() определяется в обёртке
 * ОСРВ (FreeRTOS: xTaskGetTickCount(), Zephyr: k_uptime_get_32()).
 *
 * TinyGPS++.cpp компилируется с -DARDUINO=1, чтобы он НЕ определял свою
 * chrono-версию millis() (иначе - конфликт символов с RTOS-версией данной обёртки).
 */

#ifndef LIGHT_NMEA_RTOS_ARDUINO_SHIM_H
#define LIGHT_NMEA_RTOS_ARDUINO_SHIM_H

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Определяется в обёртке ОСРВ (см. freertos/src/main.cpp, zephyr/src/main.cpp).
unsigned long millis();

// Arduino-типы
typedef uint8_t byte;

// Arduino-математические макросы
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef TWO_PI
#define TWO_PI (2.0 * PI)
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

#endif // LIGHT_NMEA_RTOS_ARDUINO_SHIM_H