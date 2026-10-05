/**
 * @file utils.hpp
 * @brief FPU-free, constexpr math utilities for integer-based GNSS data.
 *
 * All functions are constexpr and inline (zero runtime overhead).
 * No dependencies on <cmath> or floating-point arithmetic.
 *
 * @author LightNMEA Project
 * @license GPL-3.0
 */

#ifndef LIGHT_NMEA_UTILS_HPP
#define LIGHT_NMEA_UTILS_HPP

#include <cstdint>
#include <climits> // For INT32_MAX, INT32_MIN

namespace light_nmea
{
namespace utils
{

// ============================================================================
// 1. CONSTANTS & SCALES
// ============================================================================

// --- Coordinate Scales & Limits ---
constexpr int32_t COORD_SCALE = 10'000'000; // 1e-7 degrees

constexpr int32_t MIN_LAT = -90 * COORD_SCALE;
constexpr int32_t MAX_LAT =  90 * COORD_SCALE;
constexpr int32_t MIN_LON = -180 * COORD_SCALE;
constexpr int32_t MAX_LON =  180 * COORD_SCALE;

// --- Heading Scales & Limits ---
constexpr int32_t HEADING_SCALE = 100;      // 1e-2 degrees

constexpr int32_t HEADING_FULL = 360 * HEADING_SCALE;
constexpr int32_t HEADING_HALF = 180 * HEADING_SCALE;
constexpr int32_t HEADING_QUARTER = 90 * HEADING_SCALE;

// --- General Math Scales ---
constexpr int32_t PERCENT_SCALE = 100;      // For lerp, ema, etc.

// --- Distance Approximation ---
constexpr int32_t METERS_PER_DEGREE_LAT = 111'320; // ~111.32 km at equator
constexpr int32_t POLE_LATITUDE = 90 * COORD_SCALE;
constexpr int32_t LON_SCALE_MAX = 1000;            // Max longitude scale factor

// --- Speed Conversion Factors ---
// Knots to cm/s (1 knot = 51.4444 cm/s)
constexpr int32_t KNOTS_TO_CM_S_NUM = 514'444;
constexpr int32_t KNOTS_TO_CM_S_DEN = 10'000;

// km/h to cm/s (1 km/h = 27.7778 cm/s)
constexpr int32_t KMH_TO_CM_S_NUM = 277'778;
constexpr int32_t KMH_TO_CM_S_DEN = 10'000;

// m/s to cm/s
constexpr int32_t MS_TO_CM_S = 100;

// cm/s to km/h (1 cm/s = 0.036 km/h)
constexpr int32_t CM_S_TO_KMH_NUM = 36;
constexpr int32_t CM_S_TO_KMH_DEN = 10;

// cm/s to knots (1 cm/s = 1/51.4444 knots)
constexpr int32_t CM_S_TO_KNOTS_NUM = 1'000'000;
constexpr int32_t CM_S_TO_KNOTS_DEN = 514'444;

// ============================================================================
// 2. VALIDATION
// ============================================================================

constexpr bool is_valid_latitude(int32_t lat) noexcept
{
    return (lat >= MIN_LAT) && (lat <= MAX_LAT);
}

constexpr bool is_valid_longitude(int32_t lon) noexcept
{
    return (lon >= MIN_LON) && (lon <= MAX_LON);
}

constexpr bool is_valid_coord(int32_t lat, int32_t lon) noexcept
{
    return is_valid_latitude(lat) && is_valid_longitude(lon)
           && (lat != 0) && (lon != 0);
}

constexpr bool is_valid_heading(int32_t heading) noexcept
{
    return (heading >= 0) && (heading < HEADING_FULL);
}

constexpr bool is_valid_speed(int32_t speed_cm_s) noexcept
{
    return speed_cm_s >= 0;
}

// ============================================================================
// 3. COMPARISON WITH DEADBAND (HYSTERESIS)
// ============================================================================

constexpr bool has_changed(int32_t current, int32_t last, int32_t threshold) noexcept
{
    int32_t diff = current - last;
    return (diff > threshold) || (diff < -threshold);
}

constexpr bool coord_has_changed(int32_t lat1, int32_t lon1,
                                 int32_t lat2, int32_t lon2,
                                 int32_t threshold) noexcept
{
    return has_changed(lat1, lat2, threshold)
        || has_changed(lon1, lon2, threshold);
}

constexpr bool speed_has_changed(int32_t current, int32_t last,
                                 int32_t threshold_cm_s) noexcept
{
    return has_changed(current, last, threshold_cm_s);
}

// ============================================================================
// 4. HEADING MATH
// ============================================================================

constexpr int32_t normalize_heading(int32_t heading) noexcept
{
    heading = heading % HEADING_FULL;
    if (heading < 0)
    {
        heading += HEADING_FULL;
    }
    return heading;
}

constexpr int32_t heading_diff(int32_t target, int32_t current) noexcept
{
    int32_t diff = normalize_heading(target - current);
    if (diff > HEADING_HALF)
    {
        diff -= HEADING_FULL;
    }
    return diff;
}

constexpr int32_t heading_distance(int32_t a, int32_t b) noexcept
{
    int32_t d = heading_diff(a, b);
    return (d < 0) ? -d : d;
}

constexpr bool headings_match(int32_t a, int32_t b, int32_t tolerance) noexcept
{
    return heading_distance(a, b) <= tolerance;
}

constexpr int32_t average_heading(int32_t a, int32_t b) noexcept
{
    int32_t diff = heading_diff(b, a);
    return normalize_heading(a + diff / 2);
}

constexpr int32_t interpolate_heading(int32_t a, int32_t b, int32_t t) noexcept
{
    int32_t diff = heading_diff(b, a);
    return normalize_heading(a + (diff * t) / PERCENT_SCALE);
}

// ============================================================================
// 5. DISTANCE (Fast, FPU-free approximations)
// ============================================================================

constexpr int64_t coord_distance_sq(int32_t lat1, int32_t lon1,
                                    int32_t lat2, int32_t lon2) noexcept
{
    int64_t dlat = static_cast<int64_t>(lat1) - static_cast<int64_t>(lat2);
    int64_t dlon = static_cast<int64_t>(lon1) - static_cast<int64_t>(lon2);
    return (dlat * dlat) + (dlon * dlon);
}

constexpr int64_t coord_manhattan(int32_t lat1, int32_t lon1,
                                  int32_t lat2, int32_t lon2) noexcept
{
    int64_t dlat = static_cast<int64_t>(lat1) - static_cast<int64_t>(lat2);
    int64_t dlon = static_cast<int64_t>(lon1) - static_cast<int64_t>(lon2);
    if (dlat < 0) dlat = -dlat;
    if (dlon < 0) dlon = -dlon;
    return dlat + dlon;
}

constexpr int32_t distance_meters_approx(int32_t lat1, int32_t lon1,
                                         int32_t lat2, int32_t lon2) noexcept
{
    int64_t dlat = static_cast<int64_t>(lat1) - static_cast<int64_t>(lat2);
    int64_t dlon = static_cast<int64_t>(lon1) - static_cast<int64_t>(lon2);

    int64_t dy = (dlat * METERS_PER_DEGREE_LAT) / COORD_SCALE;

    int32_t abs_lat = (lat1 < 0) ? -lat1 : lat1;
    int32_t lat_factor = (POLE_LATITUDE - abs_lat);
    int32_t lon_scale = (lat_factor * LON_SCALE_MAX) / POLE_LATITUDE;

    int64_t dx = (dlon * METERS_PER_DEGREE_LAT * lon_scale) / (COORD_SCALE * LON_SCALE_MAX);

    int64_t dist = dy + dx;
    if (dist < 0) dist = -dist;

    return static_cast<int32_t>(dist);
}

// ============================================================================
// 6. BOUNDING & REGION CHECKS
// ============================================================================

constexpr bool in_bbox(int32_t lat, int32_t lon,
                       int32_t min_lat, int32_t min_lon,
                       int32_t max_lat, int32_t max_lon) noexcept
{
    return (lat >= min_lat) && (lat <= max_lat)
        && (lon >= min_lon) && (lon <= max_lon);
}

constexpr bool in_radius(int32_t lat, int32_t lon,
                         int32_t center_lat, int32_t center_lon,
                         int64_t radius_sq) noexcept
{
    return coord_distance_sq(lat, lon, center_lat, center_lon) <= radius_sq;
}

constexpr bool in_meters_radius(int32_t lat, int32_t lon,
                                int32_t center_lat, int32_t center_lon,
                                int32_t radius_m) noexcept
{
    int32_t units_per_m_lat = COORD_SCALE / METERS_PER_DEGREE_LAT;
    int64_t radius_units_lat = static_cast<int64_t>(radius_m) * units_per_m_lat;
    int64_t radius_sq = radius_units_lat * radius_units_lat;

    return coord_distance_sq(lat, lon, center_lat, center_lon) <= radius_sq;
}

// ============================================================================
// 7. UNIT CONVERSION
// ============================================================================

constexpr int32_t knots_to_cm_s(int32_t knots_x100) noexcept
{
    return (static_cast<int64_t>(knots_x100) * KNOTS_TO_CM_S_NUM) / KNOTS_TO_CM_S_DEN;
}

constexpr int32_t kmh_to_cm_s(int32_t kmh_x100) noexcept
{
    return (static_cast<int64_t>(kmh_x100) * KMH_TO_CM_S_NUM) / KMH_TO_CM_S_DEN;
}

constexpr int32_t ms_to_cm_s(int32_t ms_x100) noexcept
{
    return ms_x100 * MS_TO_CM_S;
}

constexpr int32_t cm_s_to_kmh(int32_t cm_s) noexcept
{
    return (static_cast<int64_t>(cm_s) * CM_S_TO_KMH_NUM) / CM_S_TO_KMH_DEN;
}

constexpr int32_t cm_s_to_knots(int32_t cm_s) noexcept
{
    return (static_cast<int64_t>(cm_s) * CM_S_TO_KNOTS_NUM) / CM_S_TO_KNOTS_DEN;
}

constexpr int32_t coord_downscale(int32_t coord, int factor = 100) noexcept
{
    return coord / factor;
}

constexpr int32_t coord_upscale(int32_t coord, int factor = 100) noexcept
{
    return coord * factor;
}

// ============================================================================
// 8. SAFE ARITHMETIC (overflow protection)
// ============================================================================

constexpr int64_t safe_diff(int32_t a, int32_t b) noexcept
{
    return static_cast<int64_t>(a) - static_cast<int64_t>(b);
}

constexpr int32_t safe_add(int32_t a, int32_t b) noexcept
{
    int64_t result = static_cast<int64_t>(a) + static_cast<int64_t>(b);
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(result);
}

constexpr int32_t safe_mul(int32_t a, int32_t b) noexcept
{
    int64_t result = static_cast<int64_t>(a) * static_cast<int64_t>(b);
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(result);
}

constexpr int32_t safe_div(int32_t a, int32_t b) noexcept
{
    return (b != 0) ? (a / b) : 0;
}

// ============================================================================
// 9. INTERPOLATION & SMOOTHING
// ============================================================================

constexpr int32_t lerp(int32_t a, int32_t b, int32_t t) noexcept
{
    if (t < 0) t = 0;
    if (t > PERCENT_SCALE) t = PERCENT_SCALE;
    return a + ((b - a) * t) / PERCENT_SCALE;
}

constexpr void lerp_coord(int32_t lat1, int32_t lon1,
                          int32_t lat2, int32_t lon2,
                          int32_t t,
                          int32_t& out_lat, int32_t& out_lon) noexcept
{
    out_lat = lerp(lat1, lat2, t);
    out_lon = lerp(lon1, lon2, t);
}

constexpr int32_t ema(int32_t old_value, int32_t new_value, int32_t alpha) noexcept
{
    if (alpha <= 0) return old_value;
    if (alpha >= PERCENT_SCALE) return new_value;
    return old_value + ((new_value - old_value) * alpha) / PERCENT_SCALE;
}

constexpr int32_t midpoint(int32_t a, int32_t b) noexcept
{
    return (a + b) / 2;
}

// ============================================================================
// 10. STATISTICS & AGGREGATION
// ============================================================================

constexpr int32_t min_val(int32_t a, int32_t b) noexcept
{
    return (a < b) ? a : b;
}

constexpr int32_t max_val(int32_t a, int32_t b) noexcept
{
    return (a > b) ? a : b;
}

constexpr int32_t clamp(int32_t value, int32_t min_v, int32_t max_v) noexcept
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

constexpr int32_t clamp_latitude(int32_t lat) noexcept
{
    return clamp(lat, MIN_LAT, MAX_LAT);
}

constexpr int32_t clamp_longitude(int32_t lon) noexcept
{
    return clamp(lon, MIN_LON, MAX_LON);
}

constexpr int32_t clamp_heading(int32_t heading) noexcept
{
    return normalize_heading(heading);
}

constexpr int32_t clamp_speed(int32_t speed_cm_s) noexcept
{
    return (speed_cm_s < 0) ? 0 : speed_cm_s;
}

constexpr int32_t abs_val(int32_t v) noexcept
{
    return (v < 0) ? -v : v;
}

constexpr int32_t sign_val(int32_t v) noexcept
{
    return (v > 0) ? 1 : ((v < 0) ? -1 : 0);
}

} // namespace utils
} // namespace light_nmea

#endif // LIGHT_NMEA_UTILS_HPP