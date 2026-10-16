/*
 * arduino.h — минимальная эмуляция среды Arduino для сборки TinyGPSPlus
 * вне Arduino IDE. Содержит только те элементы, которые использует библиотека:
 * millis() для времени, тип byte, нужные макросы, а также доступ к математике
 * (chrono, math.h). Это позволяет собрать проект в другой среде без лишних зависимостей.
 */
#ifndef LIGHT_NMEA_ARDUINO_SHIM_H
#define LIGHT_NMEA_ARDUINO_SHIM_H

#include <chrono>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Совпадает с реализацией в TinyGPS++.cpp (сигнатура + линк)
unsigned long millis();

// Типы Arduino
typedef uint8_t byte;

// Математические макросы Arduino
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

#endif // LIGHT_NMEA_ARDUINO_SHIM_H