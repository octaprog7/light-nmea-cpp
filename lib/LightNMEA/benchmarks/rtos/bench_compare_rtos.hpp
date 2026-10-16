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
 * @file bench_compare_rtos.hpp
 * @brief Общее портируемое ядро сравнительного бенчмарка NMEA-парсеров под ОСРВ.
 *
 * Перенос горячих циклов и логики замера из
 * benchmarks/firmware/src/bench_compare_mcu.cpp (Arduino) в RTOS-независимый
 * вид. Различия между FreeRTOS и Zephyr сводятся к двум включениям в
 * BenchEnv:
 *   - now_us(): источник времени в микросекундах
 *               (FreeRTOS: clock_gettime / цикловой счётчик;
 *                Zephyr:   k_cycle_get_32() + k_cyc_to_us_ceil32());
 *   - print():  printf-совместимый вывод (printf / printk).
 *
 * Сравниваются три парсера на идентичных пакетах из nav_gen::NAV_PACKETS:
 *   - LightNMEA   (эта библиотека, header-only, целочисленный);
 *   - minmea      (kosma/minmea, чистый C);
 *   - TinyGPSPlus (mikalhart/TinyGPSPlus, побайтовый encode()).
 *
 * Метрики: pkt/s, ns/pkt, cycles/pkt, pkt/s на МГц. Best-of-N прогонов.
 * Горячие циклы не используют heap и FPU - работают на Cortex-M0+ и RISC-V.
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "light_nmea.hpp"
#include "nav_gen.hpp"

#include "minmea.h"
#include "TinyGPS++.h"

// F_CPU задаётся обёрткой ОСРВ (Zephyr: CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
// FreeRTOS на МК: тактовая частота ядра). По умолчанию - 100 МГц (на хосте
// метрика cycles/pkt виртуальная).
#ifndef F_CPU
#define F_CPU 100000000UL
#endif

// Конфигурация (переопределяется параметром build flags при необходимости)
#ifndef BENCH_PACKET_COUNT
#define BENCH_PACKET_COUNT 30000UL  // пакетов за прогон
#endif
#ifndef BENCH_PASSES
#define BENCH_PASSES 3              // best-of-N прогонов
#endif

namespace bench_rtos {

// Индексы пакетов в nav_gen::NAV_PACKETS, которые понимают ВСЕ три парсера:
// RMC/GGA/VTG/GLL от talker'ов GP/GN (без GSV, BD/QZ/IR, invalid-кадров).
static constexpr std::array<uint8_t, 8> PKT_IDX = {{0, 1, 2, 3, 19, 20, 22, 23}};

// Sink: мешает компилятору выкинуть "бесполезные" горячие циклы.
inline volatile uint32_t g_sink = 0;

enum class Impl : uint8_t { LIGHT, MINMEA, TINYGPS };

// ============================================================================
// Горячие циклы по реализациям
// ============================================================================
static uint32_t run_lightnmea(uint32_t total) noexcept {
  light_nmea::Parser parser;
  uint32_t ok = 0;
  for (uint32_t i = 0; i < total; ++i) {
    if (parser.parse_line(nav_gen::NAV_PACKETS[PKT_IDX[i % PKT_IDX.size()]])) {
      ++ok;
    }
  }
  g_sink += ok;
  return ok;
}

static uint32_t run_minmea(uint32_t total) noexcept {
  uint32_t ok = 0;
  for (uint32_t i = 0; i < total; ++i) {
    const char *line =
        nav_gen::NAV_PACKETS[PKT_IDX[i % PKT_IDX.size()]].data();
    const char t1 = line[3], t2 = line[4], t3 = line[5];
    if (t1 == 'R' && t2 == 'M' && t3 == 'C') {
      minmea_sentence_rmc f{};
      if (minmea_parse_rmc(&f, line)) ++ok;
    } else if (t1 == 'G' && t2 == 'G' && t3 == 'A') {
      minmea_sentence_gga f{};
      if (minmea_parse_gga(&f, line)) ++ok;
    } else if (t1 == 'V' && t2 == 'T' && t3 == 'G') {
      minmea_sentence_vtg f{};
      if (minmea_parse_vtg(&f, line)) ++ok;
    } else if (t1 == 'G' && t2 == 'L' && t3 == 'L') {
      minmea_sentence_gll f{};
      if (minmea_parse_gll(&f, line)) ++ok;
    }
  }
  g_sink += ok;
  return ok;
}

static uint32_t run_tinygps(uint32_t total) noexcept {
  TinyGPSPlus gps;
  uint32_t ok = 0;
  for (uint32_t i = 0; i < total; ++i) {
    const std::string_view sv =
        nav_gen::NAV_PACKETS[PKT_IDX[i % PKT_IDX.size()]];
    for (const char c : sv) {
      if (gps.encode(c)) ++ok;  // encode() == true на завершённой сентенции
    }
  }
  g_sink += ok;
  return ok;
}

static uint32_t run_impl(Impl impl, uint32_t total) noexcept {
  switch (impl) {
    case Impl::LIGHT: return run_lightnmea(total);
    case Impl::MINMEA: return run_minmea(total);
    case Impl::TINYGPS: return run_tinygps(total);
  }
  return 0;
}

// ============================================================================
// Замер (best-of-N) и результат
// ============================================================================
struct BenchResult {
  const char *name;
  uint32_t pkt_per_s = 0;
  uint32_t ns_per_pkt = 0;
  uint32_t cycles_per_pkt = 0;      // округлённые циклы на пакет
  uint32_t pkt_per_s_per_mhz = 0;   // pkt/s на 1 МГц тактовой частоты
  uint32_t ok = 0;
};

// Окружение ОСРВ: источник времени (мкс) и printf-совместимый вывод.
using NowUsFn = uint64_t (*)();
using PrintFn = int (*)(const char *fmt, ...);

struct BenchEnv {
  NowUsFn now_us;
  PrintFn print;
};

// Полностью целочисленный расчёт (без FPU-эмуляции на Cortex-M0+ и без %f
// в printf, который тянет float-поддержку newlib в прошивку).
static BenchResult bench_impl(Impl impl, const char *name,
                              const BenchEnv &env) noexcept {
  uint64_t best_dt = UINT64_MAX;
  uint32_t best_ok = 0;
  for (uint32_t pass = 0; pass < BENCH_PASSES; ++pass) {
    const uint64_t t0 = env.now_us();
    const uint32_t ok = run_impl(impl, BENCH_PACKET_COUNT);
    const uint64_t dt = env.now_us() - t0;
    if (dt < best_dt) {
      best_dt = dt;
      best_ok = ok;
    }
  }
  if (best_dt == 0) best_dt = 1;
  if (best_dt > 0xFFFFFFFFull) best_dt = 0xFFFFFFFFull;  // защита от переполнения

  const uint32_t dt = static_cast<uint32_t>(best_dt);
  const uint32_t mhz = static_cast<uint32_t>(F_CPU) / 1000000UL;
  BenchResult r;
  r.name = name;
  // pkt/s = packets * 1'000'000 / dt_us  (в 64-битной арифметике)
  r.pkt_per_s = static_cast<uint32_t>(
      static_cast<uint64_t>(BENCH_PACKET_COUNT) * 1000000ULL / dt);
  r.ns_per_pkt = dt * 1000UL / BENCH_PACKET_COUNT;
  // cycles/pkt = ns/pkt * MHz / 1000.
  // mhz == 0 возможен, когда цикловой счётчик медленнее 1 МГц
  // (например, RTC 32.768 кГц на nRF52840) - метрики обнуляются.
  r.cycles_per_pkt = mhz ? (r.ns_per_pkt * mhz + 500UL) / 1000UL : 0;
  r.pkt_per_s_per_mhz = mhz ? r.pkt_per_s / mhz : 0;
  r.ok = best_ok;
  return r;
}

static void print_result(const BenchResult &r, const BenchEnv &env) noexcept {
  env.print("%-14s %10lu %8lu %10lu %10lu  %lu/%lu\n",
            r.name,
            static_cast<unsigned long>(r.pkt_per_s),
            static_cast<unsigned long>(r.ns_per_pkt),
            static_cast<unsigned long>(r.cycles_per_pkt),
            static_cast<unsigned long>(r.pkt_per_s_per_mhz),
            static_cast<unsigned long>(r.ok),
            static_cast<unsigned long>(BENCH_PACKET_COUNT));
}

// ============================================================================
// Полный прогон: три парсера, best-of-N, таблица результатов
// ============================================================================
static void run_benchmark(const BenchEnv &env) noexcept {
  env.print("=== LightNMEA RTOS comparative benchmark ===\n");
  env.print("Packets/pass: %lu, passes: %lu (best), F_CPU: %lu Hz\n",
            static_cast<unsigned long>(BENCH_PACKET_COUNT),
            static_cast<unsigned long>(BENCH_PASSES),
            static_cast<unsigned long>(F_CPU));
  env.print("\n");

  const BenchResult rl = bench_impl(Impl::LIGHT, "LightNMEA", env);
  const BenchResult rm = bench_impl(Impl::MINMEA, "minmea", env);
  const BenchResult rt = bench_impl(Impl::TINYGPS, "TinyGPSPlus", env);

  env.print("Implementation  pkt/s       ns/pkt  cycles/pkt  pkt/s/MHz  ok/total\n");
  print_result(rl, env);
  print_result(rm, env);
  print_result(rt, env);
  env.print("\n");
}

} // namespace bench_rtos