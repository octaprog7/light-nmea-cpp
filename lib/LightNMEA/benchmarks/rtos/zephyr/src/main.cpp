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
 * @file main.cpp
 * @brief Zephyr-обёртка сравнительного бенчмарка NMEA-парсеров.
 *
 * Запускает bench_rtos::run_benchmark() в главном потоке Zephyr.
 * Источник времени - k_cycle_get_32() + k_cyc_to_us_ceil32() (аппаратный
 * цикловой счётчик; на native_sim - симулированный).
 *
 * F_CPU для метрики cycles/pkt: по умолчанию частота циклового счётчика
 * Zephyr (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC). Частоту CPU для конкретной
 * платы задайте с помощью F_CPU CMake (см. CMakeLists.txt: nRF52840 -> 64 МГц).
 *
 * Сборка (см. README.md):
 *   west build -b native_sim -d build/native_sim
 *   west build -b nrf52840dk/nrf52840 -d build/nrf52840dk
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <cstdarg>

#ifndef F_CPU
#define F_CPU CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
#endif

#include "bench_compare_rtos.hpp"

// ============================================================================
// millis() для TinyGPSPlus (объявлена в Arduino.h-обёртке)
// ============================================================================
unsigned long millis() {
  return static_cast<unsigned long>(k_uptime_get_32());
}

// ============================================================================
// Источник времени в микросекундах (цикловой счётчик Zephyr)
// ============================================================================
static uint64_t now_us() {
  return static_cast<uint64_t>(k_cyc_to_us_ceil32(k_cycle_get_32()));
}

// ============================================================================
// printf-совместимый вывод поверх printk (printk возвращает void)
// ============================================================================
static int printk_adapter(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintk(fmt, ap);
  va_end(ap);
  return 0;
}

// ============================================================================
// main
// ============================================================================
int main(void) {
  const bench_rtos::BenchEnv env{now_us, printk_adapter};
  bench_rtos::run_benchmark(env);
  return 0;
}