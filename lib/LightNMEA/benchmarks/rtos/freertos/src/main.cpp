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
 * @brief FreeRTOS-обёртка сравнительного бенчмарка NMEA-парсеров.
 *
 * Запускает bench_rtos::run_benchmark() в задаче FreeRTOS (POSIX-порт, хост).
 * Источник времени - clock_gettime(CLOCK_MONOTONIC) с точностью до мкс.
 *
 * Для реального железа замените now_us() на цикловой счётчик платформы:
 *   - Cortex-M:  DWT->CYCCNT (нужен CoreDebug->DEMCR |= TRCENA);
 *   - ESP32:     esp_timer_get_time();
 *   - RISC-V:    mcycle / rdcycle.
 * и укажите свой FreeRTOSConfig.h + порт (вместо POSIX).
 *
 * Сборка: см. CMakeLists.txt. Запуск из каталога benchmarks/rtos/freertos:
 *   ./build/freertos_nmea_bench
 */

#include "FreeRTOS.h"
#include "task.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "bench_compare_rtos.hpp"

// ============================================================================
// millis() для TinyGPSPlus (объявлена в Arduino.h-обёртке)
// ============================================================================
unsigned long millis() {
  return static_cast<unsigned long>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// ============================================================================
// Источник времени в микросекундах (POSIX-порт: clock_gettime)
// ============================================================================
static uint64_t now_us() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
         static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

// ============================================================================
// Задача-бенчмарк
// ============================================================================
static void bench_task(void *) {
  const bench_rtos::BenchEnv env{now_us, std::printf};
  bench_rtos::run_benchmark(env);
  std::printf("Done. Reboot to re-run.\n");
  std::fflush(stdout);
  // POSIX-порт не завершает процесс сам после удаления всех задач.
  std::exit(0);
}

// ============================================================================
// main: создание задачи и запуск планировщика
// ============================================================================
int main() {
  xTaskCreate(bench_task, "bench", 4096, nullptr, 1, nullptr);
  std::printf("LightNMEA FreeRTOS benchmark: starting scheduler\n");
  vTaskStartScheduler();

  // Тут управление не возвращается (POSIX-порт завершает процесс).
  return 0;
}