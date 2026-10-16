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

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace nav_gen {

// Набор NMEA-пакетов (из Python версии)
// RMC, GGA, VTG и GLL + \r\n:
inline constexpr std::array<std::string_view, 24> NAV_PACKETS = {{
    //  1. Munich RMC
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n",
    //  2. Munich GGA
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n",
    //  3. Moscow RMC
    "$GNRMC,091530,A,5545.1234,N,03737.5678,E,015.0,120.5,220626,,,A*69\r\n",
    //  4. Moscow GGA
    "$GNGGA,091530.00,5545.1234,N,03737.5678,E,1,12,0.8,156.3,M,14.2,M,,*78\r\n",
    //  5. Sydney RMC
    "$GNRMC,235959,A,3351.5432,S,15112.3456,E,045.7,270.0,311225,,,A*71\r\n",
    //  6. Sydney GGA
    "$GNGGA,235959.99,3351.5432,S,15112.3456,E,2,10,1.1,58.0,M,0.0,M,,*67\r\n",
    //  7. New York RMC
    "$GPRMC,180000,A,4042.8345,N,07400.2456,W,008.2,045.0,150725,,,A*6A\r\n",
    //  8. New York GGA
    "$GPGGA,180000.50,4042.8345,N,07400.2456,W,1,09,1.0,10.5,M,-34.0,M,,*6A\r\n",
    //  9. Equator RMC
    "$GNRMC,000000,A,0000.0000,N,00000.0000,E,000.0,000.0,010126,,,A*6A\r\n",
    // 10. Invalid fix (status V)
    "$GNRMC,120000,V,4807.038,N,01131.000,E,000.0,000.0,220626,,,N*75\r\n",
    // 11. RTK Fixed (quality=4)
    "$GNGGA,120000.00,5545.1234,N,03737.5678,E,4,15,0.5,156.3,M,14.2,M,,*7A\r\n",
    // 12. RTK Float (quality=5)
    "$GNGGA,120000.00,5545.1234,N,03737.5678,E,5,12,0.7,156.3,M,14.2,M,,*7E\r\n",
    // 13. No fix (quality=0)
    "$GNGGA,120000.00,,,,,0,00,99.99,,,,,,*7B\r\n",
    // 14. Very high HDOP (poor accuracy)
    "$GPGGA,123519,4807.038,N,01131.000,E,1,03,15.0,545.4,M,46.9,M,,*71\r\n",
    // 15. Maximum length packet (GSV with 12 satellites)
    "$GPGSV,1,1,12,01,45,090,45,02,30,180,40,03,20,270,35,04,10,360,30*7F\r\n",
    // 16. BeiDou (new BD format)
    "$BDRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*7B\r\n",
    // 17. QZSS (new QZ format)
    "$QZRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*76\r\n",
    // 18. NavIC (new IR format)
    "$IRRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*66\r\n",
    // 19. VTG - course and speed (Munich)
    "$GPVTG,084.4,T,081.3,M,022.4,N,041.5,K,A*25\r\n",
    // 20. VTG - high speed (Moscow)
    "$GNVTG,120.5,T,117.4,M,015.0,N,027.8,K,A*31\r\n",
    // 21. VTG - without Mode Indicator (old format)
    "$GPVTG,270.0,T,267.0,M,045.7,N,084.6,K*44\r\n",
    // 22. GLL - coordinates (Munich)
    "$GPGLL,4807.038,N,01131.000,E,123519,A,A*48\r\n",
    // 23. GLL - coordinates (Moscow)
    "$GNGLL,5545.1234,N,03737.5678,E,091530,A,A*5E\r\n",
    // 24. GLL - southern hemisphere (Sydney)
    "$GNGLL,3351.5432,S,15112.3456,E,235959,A,A*43\r\n",
}};

/**
 * @brief Быстрая функция для получения случайного пакета (без создания объекта).
 * @return std::string_view Случайный NMEA пакет.
 */
inline std::string_view get_nav_packet() {
    thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, NAV_PACKETS.size() - 1);
    return NAV_PACKETS[dist(gen)];
}

} // namespace nav_gen
