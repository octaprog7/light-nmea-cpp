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
    constexpr int32_t SPEED   = 1000;      // универсальный масштаб x1000 (узлыx1000, км/чx1000)
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

    // Составное сложение/вычитание (с нормализацией)
    constexpr Heading& operator+=(const Heading& o) noexcept { raw = normalize(raw + o.raw); return *this; }
    constexpr Heading& operator-=(const Heading& o) noexcept { raw = normalize(raw - o.raw); return *this; }

    constexpr bool operator==(const Heading& o) const noexcept { return raw == o.raw; }
    constexpr bool operator!=(const Heading& o) const noexcept { return raw != o.raw; }
};

/**
 * @brief Скорость (Speed).
 * Универсальный целочисленный масштаб x1000: тысячные узла, тысячные км/ч и т.п.
 * Единицы определяются контекстом (см. speed_kn_x1000 / speed_kmh_x1000 в парсере).
 */
struct Speed
{
    int32_t raw = 0; // значение в масштабе x1000

    constexpr Speed() = default;
    constexpr explicit Speed(int32_t raw_val) : raw(raw_val) {}

    constexpr Speed operator+(const Speed& o) const noexcept { return Speed(raw + o.raw); }
    constexpr Speed operator-(const Speed& o) const noexcept { return Speed(raw - o.raw); }
    constexpr Speed& operator+=(const Speed& o) noexcept { raw += o.raw; return *this; }
    constexpr Speed& operator-=(const Speed& o) noexcept { raw -= o.raw; return *this; }
    constexpr Speed operator-() const noexcept { return Speed(-raw); }
    
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
 * @brief Точные целочисленные конверсии скорости БЕЗ FPU.
 *
 * Для MCU без аппаратного float (RP2040/Cortex-M0+, AVR и т.п.) float-умножение
 * эмулируется программно и дорого. Эти функции конвертируют узлы в км/ч и м/с
 * строго целочисленно, используя точные рациональные соотношения:
 *   1 узел = 1.852 км/ч = 1852/1000  км/ч
 *   1 узел = 1852/3600 м/с = 463/900 м/с  (точно)
 *
 * Вход/выход - в масштабе x1000 (как speed_kn_x1000 / speed_kmh_x1000),
 * деление целочисленное (отбрасывание дробного остатка).
 *
 * Пример: data.speed_kmh_x1000 из VTG можно заменить на
 *         knots_x1000_to_kmh_x1000(data.speed_kn_x1000.raw).
 */
[[nodiscard]] inline constexpr int32_t knots_x1000_to_kmh_x1000(int32_t kn_x1000) noexcept
{
    return static_cast<int32_t>(static_cast<int64_t>(kn_x1000) * 1852 / 1000);
}

[[nodiscard]] inline constexpr int32_t knots_x1000_to_ms_x1000(int32_t kn_x1000) noexcept
{
    return static_cast<int32_t>(static_cast<int64_t>(kn_x1000) * 463 / 900);
}

/**
 * @brief Полная 2D координата.
 */
struct Coordinate
{
    Latitude lat;
    Longitude lon;

    constexpr Coordinate() = default;
    constexpr Coordinate(Latitude l1, Longitude l2) : lat(l1), lon(l2) {}

    constexpr bool operator==(const Coordinate& o) const noexcept
    {
        return (lat == o.lat) && (lon == o.lon);
    }

    constexpr bool operator!=(const Coordinate& o) const noexcept { return !(*this == o); }

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