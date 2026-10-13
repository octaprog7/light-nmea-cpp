/*
 * bench_compare_mcu.cpp - on-target comparative NMEA parser benchmark
 * for Raspberry Pi Pico (RP2040) and ESP32.
 *
 * Compares three parsers on identical packets (from nav_gen.hpp):
 *   - LightNMEA   (this library, header-only, integer-only)
 *   - minmea      (kosma/minmea, plain C)
 *   - TinyGPSPlus (mikalhart/TinyGPSPlus, Arduino streaming)
 *
 * Metric: packets per second (pkt/s), ns/pkt, cycles/pkt, pkt/s per MHz.
 * Best-of-N passes are reported (the fastest pass wins).
 *
 * Output goes to Serial @ 115200. One run per boot; reboot to re-run.
 *
 * Build: see ../platformio.ini, or run ./build_bench_fw.sh from benchmarks/.
 */

#include <Arduino.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "light_nmea.hpp"
#include "nav_gen.hpp"

#include "minmea.h"
#include "TinyGPS++.h"

// F_CPU is provided by the Arduino cores; fallback for standalone builds.
#ifndef F_CPU
#define F_CPU 100000000UL
#endif

// ============================================================================
// Configuration (override via build_flags if needed)
// ============================================================================
#ifndef BENCH_PACKET_COUNT
#define BENCH_PACKET_COUNT 30000UL  // packets per pass
#endif
#ifndef BENCH_PASSES
#define BENCH_PASSES 3  // best-of-N passes
#endif

// Индексы пакетов в nav_gen::NAV_PACKETS, которые понимают ВСЕ три парсера:
// RMC/GGA/VTG/GLL от talker'ов GP/GN (без GSV, BD/QZ/IR, invalid-кадров).
static constexpr std::array<uint8_t, 8> PKT_IDX = {{0, 1, 2, 3, 19, 20, 22, 23}};

// Sink: мешает компилятору выкинуть "бесполезные" горячие циклы.
volatile uint32_t g_sink = 0;

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
      if (gps.encode(c)) ++ok;  // encode() == true on a completed sentence
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
  uint32_t cycles_per_pkt = 0;   // округлённые циклы на пакет
  uint32_t pkt_per_s_per_mhz = 0;  // pkt/s на 1 МГц тактовой частоты
  uint32_t ok = 0;
};

// Полностью целочисленный расчёт (без FPU-эмуляции на Cortex-M0+ и без
// %f в printf, который тянет float-поддержку newlib в прошивку).
static BenchResult bench_impl(Impl impl, const char *name) noexcept {
  uint32_t best_dt = 0xFFFFFFFFu;
  uint32_t best_ok = 0;
  for (uint32_t pass = 0; pass < BENCH_PASSES; ++pass) {
    const uint32_t t0 = micros();
    const uint32_t ok = run_impl(impl, BENCH_PACKET_COUNT);
    const uint32_t dt = micros() - t0;
    if (dt < best_dt) {
      best_dt = dt;
      best_ok = ok;
    }
  }
  if (best_dt == 0) best_dt = 1;

  const uint32_t mhz = static_cast<uint32_t>(F_CPU) / 1000000UL;
  BenchResult r;
  r.name = name;
  // pkt/s = packets * 1'000'000 / dt_us  (в 64-битной арифметике)
  r.pkt_per_s = static_cast<uint32_t>(
      static_cast<uint64_t>(BENCH_PACKET_COUNT) * 1000000ULL / best_dt);
  r.ns_per_pkt = best_dt * 1000UL / BENCH_PACKET_COUNT;
  // cycles/pkt = ns/pkt * MHz / 1000
  r.cycles_per_pkt =
      (r.ns_per_pkt * mhz + 500UL) / 1000UL;
  r.pkt_per_s_per_mhz = r.pkt_per_s / mhz;
  r.ok = best_ok;
  return r;
}

static void print_result(const BenchResult &r) noexcept {
  Serial.printf("%-14s %10lu %8lu %10lu %10lu  %lu/%lu\n",
                r.name,
                static_cast<unsigned long>(r.pkt_per_s),
                static_cast<unsigned long>(r.ns_per_pkt),
                static_cast<unsigned long>(r.cycles_per_pkt),
                static_cast<unsigned long>(r.pkt_per_s_per_mhz),
                static_cast<unsigned long>(r.ok),
                static_cast<unsigned long>(BENCH_PACKET_COUNT));
}

// ============================================================================
// Arduino entry points
// ============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  Serial.println();
  Serial.println("=== LightNMEA MCU comparative benchmark ===");
  Serial.print("Packets/pass: ");
  Serial.print(static_cast<unsigned long>(BENCH_PACKET_COUNT));
  Serial.print(", passes: ");
  Serial.print(static_cast<unsigned long>(BENCH_PASSES));
  Serial.print(" (best), F_CPU: ");
  Serial.print(static_cast<unsigned long>(F_CPU));
  Serial.println(" Hz");
  Serial.println();

  const BenchResult rl = bench_impl(Impl::LIGHT, "LightNMEA");
  const BenchResult rm = bench_impl(Impl::MINMEA, "minmea");
  const BenchResult rt = bench_impl(Impl::TINYGPS, "TinyGPSPlus");

  Serial.println("Implementation  pkt/s       ns/pkt  cycles/pkt  pkt/s/MHz  ok/total");
  print_result(rl);
  print_result(rm);
  print_result(rt);
  Serial.println();
  Serial.println("Done. Reboot the board to re-run.");
}

void loop() {
  delay(1000);  // idle after measurement; reboot to re-run
}