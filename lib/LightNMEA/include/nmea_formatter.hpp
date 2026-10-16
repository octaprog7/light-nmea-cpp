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

#pragma once

#include "light_nmea.hpp"
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace light_nmea {

// ============================================================================
// Единицы измерения
// ============================================================================
enum class SpeedUnit : uint8_t { KMH = 0, KNOTS = 1, MS = 2 };

enum class DistanceUnit : uint8_t { METERS = 0, FEET = 1, KILOMETERS = 2 };

enum class Format : uint8_t { TXT = 0, CSV = 1, JSON = 2, COMPACT = 3 };

// ============================================================================
// Lookup-таблицы
// ============================================================================
namespace detail {

// Индексы должны совпадать со значениями light_nmea::Constellation:
// Unknown=0, GPS=1, GLONASS=2, Galileo=3, BeiDou=4, QZSS=5, NavIC=6, Multi=7.
inline constexpr std::array<const char *, 8> CST_NAMES = {
    "Unknown", "GPS", "GLONASS", "Galileo",
    "BeiDou",  "QZSS", "NavIC",   "Multi-GNSS"};

// Индексы должны совпадать со значениями light_nmea::FixMode:
// Autonomous=0, DGPS=1, Estimated=2, NotValid=3, RTKFixed=4, RTKFloat=5.
inline constexpr std::array<const char *, 6> FIX_NAMES = {
    "Autonomous", "DGPS", "Estimated", "Not Valid", "RTK Fixed", "RTK Float"};

inline constexpr std::array<const char *, 3> SPEED_UNIT_NAMES = {
    "km/h", "knots", "m/s"};

inline constexpr std::array<const char *, 3> DISTANCE_UNIT_NAMES = {"m", "ft",
                                                                    "km"};

template <size_t N>
inline const char *get_name(const std::array<const char *, N> &names,
                            uint8_t index) noexcept {
  return (index < N) ? names[index] : "Unknown";
}

} // namespace detail

// ============================================================================
// BufferWriter с operator<<
// ============================================================================
class BufferWriter {
public:
  BufferWriter(char *buffer, size_t size) noexcept
      : buffer_(buffer), size_(size), pos_(0) {
    if (size_ > 0)
      buffer_[0] = '\0';
  }

  // === Перегрузки operator<< ===

  // Строки
  BufferWriter &operator<<(const char *str) noexcept {
    if (!str)
      return *this;
    size_t len = std::strlen(str);
    if (pos_ + len < size_ - 1) {
      std::memcpy(buffer_ + pos_, str, len);
      pos_ += len;
      buffer_[pos_] = '\0';
    }
    return *this;
  }

  // Символы
  BufferWriter &operator<<(char c) noexcept {
    if (pos_ < size_ - 1) {
      buffer_[pos_++] = c;
      buffer_[pos_] = '\0';
    }
    return *this;
  }

  // Целые числа
  BufferWriter &operator<<(uint32_t value) noexcept {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", value);
    return *this << buf;
  }

  BufferWriter &operator<<(int32_t value) noexcept {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value);
    return *this << buf;
  }

  // 8-битные целые (uint8_t/int8_t). Без перегрузок чтение
  // uint8_t (например, NMEAData::satellites) неоднозначно между
  // char / uint32_t / int32_t / bool на компиляторах AVR/ARM.
  BufferWriter &operator<<(uint8_t value) noexcept {
    return *this << static_cast<uint32_t>(value);
  }

  BufferWriter &operator<<(int8_t value) noexcept {
    return *this << static_cast<int32_t>(value);
  }

  // Float с точностью 1 знак
  BufferWriter &operator<<(float value) noexcept {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", value);
    return *this << buf;
  }

  // Double с точностью 6 знаков (для координат)
  BufferWriter &operator<<(double value) noexcept {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", value);
    return *this << buf;
  }

  // Bool
  BufferWriter &operator<<(bool value) noexcept {
    return *this << (value ? "true" : "false");
  }

  // === Манипуляторы (как std::endl) ===

  // Перевод строки
  BufferWriter &nl() noexcept { return *this << '\n'; }

  // Запятая (для CSV/JSON)
  BufferWriter &comma() noexcept { return *this << ','; }

  // Двоеточие (для JSON)
  BufferWriter &colon() noexcept { return *this << ':'; }

  // Кавычка (для JSON строк)
  BufferWriter &quote() noexcept { return *this << '"'; }

  // Пробел
  BufferWriter &space() noexcept { return *this << ' '; }

  // Равно (для Compact)
  BufferWriter &equals() noexcept { return *this << '='; }

  // Градус
  BufferWriter &degree() noexcept { return *this << ""; }

  // === Форматированный вывод ===

  // Аналог snprintf
  BufferWriter &fmt(const char *format, ...) noexcept {
    if (pos_ >= size_ - 1)
      return *this;

    va_list args;
    va_start(args, format);
    int written = std::vsnprintf(buffer_ + pos_, size_ - pos_, format, args);
    va_end(args);

    if (written > 0) {
      pos_ += static_cast<size_t>(written);
      if (pos_ >= size_)
        pos_ = size_ - 1;
      buffer_[pos_] = '\0';
    }
    return *this;
  }

  // === Утилиты ===

  // Форматирование времени/даты
  BufferWriter &datetime(uint32_t value, bool is_time) noexcept {
    if (value == 0)
      return *this;

    if (is_time) {
      if (value < 10000) {
        return fmt("%06u", value);
      }
      return fmt("%02u:%02u:%02u", (value / 10000) % 100, (value / 100) % 100,
                 value % 100);
    } else {
      if (value < 100000 || value > 999999) {
        return fmt("%06u", value);
      }
      // date хранит DDMMYY (230394 = 23.03.94) - без префикса века
      return fmt("%02u.%02u.%02u", (value / 10000) % 100, (value / 100) % 100,
                 value % 100);
    }
  }

  size_t finish() noexcept {
    if (pos_ < size_)
      buffer_[pos_] = '\0';
    return pos_;
  }

private:
  char *buffer_;
  size_t size_;
  size_t pos_;
};

// ============================================================================
// Helper-функции для конвертации единиц
// ============================================================================
inline float get_speed(const NMEAData &data, SpeedUnit unit) noexcept {
  switch (unit) {
  case SpeedUnit::KMH:
    return data.speed_kmh();
  case SpeedUnit::KNOTS:
    return data.speed_knots();
  case SpeedUnit::MS:
    return data.speed_ms();
  default:
    return 0.0f;
  }
}

inline float get_altitude(const NMEAData &data, DistanceUnit unit) noexcept {
  float alt_m = data.altitude_m();
  switch (unit) {
  case DistanceUnit::METERS:
    return alt_m;
  case DistanceUnit::FEET:
    return alt_m * 3.28084f;
  case DistanceUnit::KILOMETERS:
    return alt_m / 1000.0f;
  default:
    return alt_m;
  }
}

// ============================================================================
// TXT формат
// ============================================================================
inline size_t to_txt(const NMEAData &data, char *buffer, size_t buffer_size,
                     SpeedUnit speed_unit = SpeedUnit::KMH,
                     DistanceUnit dist_unit = DistanceUnit::METERS) noexcept {
  BufferWriter w(buffer, buffer_size);

  w << (data.valid ? "Fix: Yes" : "Fix: No") << '\n';
  w << "satellites: " << data.satellites << '\n';

  if (data.has_lat_lon) {
    w << "latitude: " << data.latitude_deg() << "" << '\n';
    w << "longitude: " << data.longitude_deg() << "" << '\n';
  }

  if (data.has_speed_course) {
    float speed_val = get_speed(data, speed_unit);
    const char *unit_str =
        detail::SPEED_UNIT_NAMES[static_cast<size_t>(speed_unit)];
    w << "speed: " << speed_val << ' ' << unit_str << '\n';
    w << "course: " << data.course_true_deg() << "" << '\n';
  }

  if (data.has_altitude) {
    float alt_val = get_altitude(data, dist_unit);
    const char *unit_str =
        detail::DISTANCE_UNIT_NAMES[static_cast<size_t>(dist_unit)];
    w << "altitude: " << alt_val << ' ' << unit_str << '\n';
  }

  if (data.time_utc > 0) {
    w << "time: ";
    w.datetime(data.time_utc, true);
    w << " UTC" << '\n';
  }

  if (data.date > 0) {
    w << "date: ";
    w.datetime(data.date, false);
    w << '\n';
  }

  return w.finish();
}

// ============================================================================
// CSV формат
// ============================================================================
inline size_t to_csv(const NMEAData &data, char *buffer, size_t buffer_size,
                     SpeedUnit speed_unit = SpeedUnit::KMH,
                     DistanceUnit dist_unit = DistanceUnit::METERS) noexcept {
  BufferWriter w(buffer, buffer_size);

  w << (data.valid ? "1" : "0");
  w.comma() << data.satellites;
  w.comma();

  if (data.has_lat_lon) {
    w << data.latitude_deg();
  }
  w.comma();

  if (data.has_lat_lon) {
    w << data.longitude_deg();
  }
  w.comma();

  if (data.has_speed_course) {
    w << get_speed(data, speed_unit);
  }
  w.comma();

  if (data.has_speed_course) {
    w << data.course_true_deg();
  }
  w.comma();

  if (data.has_altitude) {
    w << get_altitude(data, dist_unit);
  }
  w.comma();

  if (data.time_utc > 0) {
    w.datetime(data.time_utc, true);
  }
  w.comma();

  if (data.date > 0) {
    w.datetime(data.date, false);
  }
  w.comma();

  w << detail::get_name(detail::CST_NAMES,
                        static_cast<uint8_t>(data.constellation));
  w.comma();
  w << detail::get_name(detail::FIX_NAMES, static_cast<uint8_t>(data.fix_mode));
  w.comma();

  if (data.hdop_e2 > 0) {
    w << data.hdop_value();
  }

  return w.finish();
}

// ============================================================================
// JSON формат
// ============================================================================
inline size_t to_json(const NMEAData &data, char *buffer, size_t buffer_size,
                      SpeedUnit speed_unit = SpeedUnit::KMH,
                      DistanceUnit dist_unit = DistanceUnit::METERS) noexcept {
  BufferWriter w(buffer, buffer_size);

  w << "{\"valid\":" << data.valid;
  w << ",\"satellites\":" << data.satellites;

  if (data.has_lat_lon) {
    w << ",\"latitude\":" << data.latitude_deg();
    w << ",\"longitude\":" << data.longitude_deg();
  }

  if (data.has_speed_course) {
    w << ",\"speed\":" << get_speed(data, speed_unit);
    w << ",\"course\":" << data.course_true_deg();
  }

  if (data.has_altitude) {
    w << ",\"altitude\":" << get_altitude(data, dist_unit);
  }

  if (data.time_utc > 0) {
    w << ",\"time\":\"";
    w.datetime(data.time_utc, true);
    w << '"';
  }

  if (data.date > 0) {
    w << ",\"date\":\"";
    w.datetime(data.date, false);
    w << '"';
  }

  w << '}';
  return w.finish();
}

// ============================================================================
// Compact формат
// ============================================================================
inline size_t
to_compact(const NMEAData &data, char *buffer, size_t buffer_size,
           SpeedUnit speed_unit = SpeedUnit::KMH,
           DistanceUnit dist_unit = DistanceUnit::METERS) noexcept {
  BufferWriter w(buffer, buffer_size);

  if (!data.has_lat_lon) {
    w << "NO_FIX satellites=" << data.satellites;
    return w.finish();
  }

  w << "FIX satellites=" << data.satellites << ' ';
  w << data.latitude_deg() << ',' << data.longitude_deg();

  if (data.has_speed_course) {
    float speed_val = get_speed(data, speed_unit);
    const char *unit_str =
        detail::SPEED_UNIT_NAMES[static_cast<size_t>(speed_unit)];
    w << " speed=" << speed_val << unit_str;
    w << " course=" << data.course_true_deg();
  }

  if (data.has_altitude) {
    float alt_val = get_altitude(data, dist_unit);
    const char *unit_str =
        detail::DISTANCE_UNIT_NAMES[static_cast<size_t>(dist_unit)];
    w << " altitude=" << alt_val << unit_str;
  }

  if (data.time_utc > 0) {
    w << " time=";
    w.datetime(data.time_utc, true);
  }

  if (data.date > 0) {
    w << " date=";
    w.datetime(data.date, false);
  }

  return w.finish();
}

// ============================================================================
// Универсальная функция
// ============================================================================
inline size_t
to_format(const NMEAData &data, char *buffer, size_t buffer_size,
          Format format = Format::TXT, SpeedUnit speed_unit = SpeedUnit::KMH,
          DistanceUnit dist_unit = DistanceUnit::METERS) noexcept {
  switch (format) {
  case Format::TXT:
    return to_txt(data, buffer, buffer_size, speed_unit, dist_unit);
  case Format::CSV:
    return to_csv(data, buffer, buffer_size, speed_unit, dist_unit);
  case Format::JSON:
    return to_json(data, buffer, buffer_size, speed_unit, dist_unit);
  case Format::COMPACT:
    return to_compact(data, buffer, buffer_size, speed_unit, dist_unit);
  default:
    buffer[0] = '\0';
    return 0;
  }
}

} // namespace light_nmea