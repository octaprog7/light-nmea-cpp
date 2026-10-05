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

#include <iostream>
#include <chrono>
#include <string_view>
#include "light_nmea.hpp"
#include "nav_gen.hpp"

// Глобальная переменная для предотвращения оптимизации цикла компилятором
volatile int64_t g_sink = 0;

void benchmark_nav_gen(size_t iterations) {
    light_nmea::Parser parser;
    
    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < iterations; ++i) {
        // Используем реальную функцию из вашего nav_gen.hpp
        std::string_view packet = nav_gen::get_nav_packet();
        
        if (parser.parse_line(packet)) {
            g_sink += parser.data().lat.raw;
            g_sink += parser.data().lon.raw;
            g_sink += parser.data().speed_kn_x1000.raw;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    double speed_kpkt = (static_cast<double>(iterations) / elapsed_ms);

    std::cout << "  [nav_gen] Time: " << elapsed_ms << " ms\n";
    std::cout << "  [nav_gen] Speed: " << speed_kpkt << " kpkt/s\n";
}

int main() {
    std::cout << "=== Running nav_gen Benchmark ===\n";
    
    // 10 миллионов итераций для ПК (стабильные цифры)
    constexpr size_t ITERATIONS = 10'000'000; 
    
    benchmark_nav_gen(ITERATIONS);
    
    std::cout << "=== Benchmark Complete. Sink: " << g_sink << " ===\n";
    return 0;
}