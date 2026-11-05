/*
 * Arduino.h - минимальная эмуляция Arduino API для сборки TinyGPSPlus
 * вне Arduino-ядра, на bare-metal AVR (avr-gcc + avr-libc).
 *
 * TinyGPS++.h делает #include "Arduino.h"; библиотеке нужны только:
 *   - тип byte;
 *   - математические макросы (PI, radians/degrees, sq);
 *   - millis() (на AVR у TinyGPSPlus собственного фолбэка НЕТ -
 *     в TinyGPS++.cpp он включается только при !defined(__AVR__)).
 *
 * Реализация millis() живёт в bench_avr.cpp.
 */
#ifndef LIGHT_NMEA_AVR_ARDUINO_SHIM_H
#define LIGHT_NMEA_AVR_ARDUINO_SHIM_H

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t byte;

// Математические макросы Arduino (как в benchmarks/compare/Arduino.h)
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

// Требуется TinyGPSPlus, реализуется в bench_avr.cpp (Timer1-based)
#ifdef __cplusplus
extern "C"
#endif
unsigned long millis();

#endif // LIGHT_NMEA_AVR_ARDUINO_SHIM_H