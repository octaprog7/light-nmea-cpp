/**
 * @file types.hpp
 * @brief Strongly-typed wrappers for GNSS integer data.
 */

#ifndef LIGHT_NMEA_TYPES_HPP
#define LIGHT_NMEA_TYPES_HPP

#include <cstdint>

namespace light_nmea
{

// Базовые константы масштаба
namespace scale
{
    constexpr int32_t COORD  = 10'000'000; // 1e-7 degrees (~1.1 cm precision)
    constexpr int32_t HEADING = 100;       // 1e-2 degrees (масштаб курса Heading)
    constexpr int32_t SPEED   = 100;       // универсальный масштаб x100 (узлыx100, км/чx100)
}

/**
 * @brief Широта (Latitude).
 * Диапазон: [-90 * 1e7, +90 * 1e7]
 */
struct Latitude
{
    int32_t raw = 0; // Значение в 1e-7 градусах

    constexpr Latitude() = default;
    constexpr explicit Latitude(int32_t raw_val) : raw(raw_val) {}

    // Операторы сравнения
    constexpr bool operator==(const Latitude& o) const noexcept { return raw == o.raw; }
    constexpr bool operator!=(const Latitude& o) const noexcept { return raw != o.raw; }
    constexpr bool operator<(const Latitude& o) const noexcept { return raw < o.raw; }
    constexpr bool operator<=(const Latitude& o) const noexcept { return raw <= o.raw; }
    constexpr bool operator>(const Latitude& o) const noexcept { return raw > o.raw; }
    constexpr bool operator>=(const Latitude& o) const noexcept { return raw >= o.raw; }

    // Валидация
    constexpr bool is_valid() const noexcept 
    {
        return (raw >= -90 * scale::COORD) && (raw <= 90 * scale::COORD);
    }
};

/**
 * @brief Долгота (Longitude).
 * Диапазон: [-180 * 1e7, +180 * 1e7]
 */
struct Longitude
{
    int32_t raw = 0;

    constexpr Longitude() = default;
    constexpr explicit Longitude(int32_t raw_val) : raw(raw_val) {}

    constexpr bool operator==(const Longitude& o) const noexcept { return raw == o.raw; }
    constexpr bool operator!=(const Longitude& o) const noexcept { return raw != o.raw; }
    constexpr bool operator<(const Longitude& o) const noexcept { return raw < o.raw; }
    constexpr bool operator<=(const Longitude& o) const noexcept { return raw <= o.raw; }
    constexpr bool operator>(const Longitude& o) const noexcept { return raw > o.raw; }
    constexpr bool operator>=(const Longitude& o) const noexcept { return raw >= o.raw; }

    constexpr bool is_valid() const noexcept 
    {
        return (raw >= -180 * scale::COORD) && (raw <= 180 * scale::COORD);
    }
};

/**
 * @brief Курс (Heading).
 * Диапазон: [0, 36000) (т.е. 0.00 .. 359.99)
 * Главная фишка: операторы сами обрабатывают переход через 360 градусов!
 */
struct Heading
{
    int32_t raw = 0; // Значение в 1e-2 градусах

    constexpr Heading() = default;
    constexpr explicit Heading(int32_t raw_val) : raw(normalize(raw_val)) {}

    // Нормализация к диапазону [0, 36000)
    static constexpr int32_t normalize(int32_t val) noexcept 
    {
        val = val % 36000;
        if (val < 0) val += 36000;
        return val;
    }

    // Вычисление кратчайшей разницы между курсами
    constexpr int32_t diff_to(const Heading& target) const noexcept 
    {
        int32_t d = target.raw - raw;
        if (d > 18000) d -= 36000;
        else if (d < -18000) d += 36000;
        return d;
    }

    // Сложение курсов (с автоматической нормализацией)
    constexpr Heading operator+(const Heading& o) const noexcept 
    {
        return Heading(normalize(raw + o.raw));
    }

    constexpr Heading operator-(const Heading& o) const noexcept 
    {
        return Heading(normalize(raw - o.raw));
    }

    constexpr bool operator==(const Heading& o) const noexcept { return raw == o.raw; }
};

/**
 * @brief Скорость (Speed).
 * Универсальный целочисленный масштаб x100: сотые узла, сотые км/ч и т.п.
 * Единицы определяются контекстом (см. speed_kn_x100 / speed_kmh_x100 в парсере).
 */
struct Speed
{
    int32_t raw = 0; // значение в масштабе x100

    constexpr Speed() = default;
    constexpr explicit Speed(int32_t raw_val) : raw(raw_val) {}

    constexpr Speed operator+(const Speed& o) const noexcept { return Speed(raw + o.raw); }
    constexpr Speed operator-(const Speed& o) const noexcept { return Speed(raw - o.raw); }
    
    constexpr bool operator==(const Speed& o) const noexcept { return raw == o.raw; }
    constexpr bool operator!=(const Speed& o) const noexcept { return raw != o.raw; }
    constexpr bool operator<(const Speed& o) const noexcept { return raw < o.raw; }
    constexpr bool operator<=(const Speed& o) const noexcept { return raw <= o.raw; }
    constexpr bool operator>(const Speed& o) const noexcept { return raw > o.raw; }
    constexpr bool operator>=(const Speed& o) const noexcept { return raw >= o.raw; }

    constexpr bool is_stopped(int32_t threshold_cm_s = 10) const noexcept 
    {
        return (raw >= -threshold_cm_s) && (raw <= threshold_cm_s);
    }
};

/**
 * @brief Полная 2D координата.
 */
struct Coordinate
{
    Latitude lat;
    Longitude lon;

    constexpr Coordinate() = default;
    constexpr Coordinate(Latitude l1, Longitude l2) : lat(l1), lon(l2) {}

    constexpr bool is_valid() const noexcept 
    {
        return lat.is_valid() && lon.is_valid() && (lat.raw != 0) && (lon.raw != 0);
    }

    // Квадрат евклидова расстояния (для быстрых сравнений без sqrt)
    constexpr int64_t distance_sq_to(const Coordinate& o) const noexcept 
    {
        int64_t dlat = static_cast<int64_t>(lat.raw) - static_cast<int64_t>(o.lat.raw);
        int64_t dlon = static_cast<int64_t>(lon.raw) - static_cast<int64_t>(o.lon.raw);
        return (dlat * dlat) + (dlon * dlon);
    }
};

} // namespace light_nmea

#endif // LIGHT_NMEA_TYPES_HPP