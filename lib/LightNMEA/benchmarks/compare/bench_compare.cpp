/*
 * bench_compare.cpp - сравнительный бенчмарк NMEA-парсеров на идентичных данных.
 *
 * Сравниваются:
 *   - LightNMEA (этот проект)
 *   - minmea (kosma/minmea, чистый C, эталон "быстрого лёгкого" парсера)
 *   - TinyGPSPlus (mikalhart/TinyGPSPlus, Arduino-ориентированный потоковый парсер)
 *
 * Условия: одинаковый набор строк, тот же CPU, тот же компилятор и флаги (-O3).
 * Метрика - строк в секунду (end-to-end разбор строки) + доля успешных.
 *
 * Использование: bench_compare [файл.nmea]
 */

#include "light_nmea.hpp"
#include "minmea.h"
#include "TinyGPS++.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace light_nmea;

// Предотвращает оптимизацию "вхолостую"
volatile int64_t g_sink = 0;

static std::vector<std::string> load_lines(const std::string &path) {
  std::vector<std::string> lines;
  std::ifstream f(path);
  if (!f.is_open()) {
    std::cerr << "Error: cannot open " << path << "\n";
    return lines;
  }
  std::string l;
  while (std::getline(f, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (!l.empty()) lines.push_back(l);
  }
  return lines;
}

struct Result {
  double seconds;
  size_t lines;
  size_t ok;
};

static inline double now_sec() {
  return std::chrono::duration<double>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// ------------------------- LightNMEA -------------------------
Result run_lightnmea(const std::vector<std::string> &lines, int iters) {
  double best = 1e30;
  size_t ok = 0;
  for (int it = 0; it < iters; ++it) {
    Parser p;
    g_sink = 0;
    size_t cur = 0;
    double t0 = now_sec();
    for (const auto &l : lines) {
      if (p.parse_line(l)) {
        ++cur;
        g_sink += p.data().lat.raw;
      }
    }
    double dt = now_sec() - t0;
    if (dt < best) { best = dt; ok = cur; }
  }
  return {best, lines.size(), ok};
}

// ------------------------- minmea -------------------------
Result run_minmea(const std::vector<std::string> &lines, int iters) {
  double best = 1e30;
  size_t ok = 0;
  for (int it = 0; it < iters; ++it) {
    g_sink = 0;
    size_t cur = 0;
    double t0 = now_sec();
    for (const auto &l : lines) {
      enum minmea_sentence_id id = minmea_sentence_id(l.c_str(), false);
      bool v = false;
      switch (id) {
        case MINMEA_SENTENCE_RMC: {
          struct minmea_sentence_rmc f;
          v = minmea_parse_rmc(&f, l.c_str());
          break;
        }
        case MINMEA_SENTENCE_GGA: {
          struct minmea_sentence_gga f;
          v = minmea_parse_gga(&f, l.c_str());
          break;
        }
        case MINMEA_SENTENCE_VTG: {
          struct minmea_sentence_vtg f;
          v = minmea_parse_vtg(&f, l.c_str());
          break;
        }
        case MINMEA_SENTENCE_GLL: {
          struct minmea_sentence_gll f;
          v = minmea_parse_gll(&f, l.c_str());
          break;
        }
        default:
          break;
      }
      if (v) {
        ++cur;
        g_sink += static_cast<int64_t>(id);
      }
    }
    double dt = now_sec() - t0;
    if (dt < best) { best = dt; ok = cur; }
  }
  return {best, lines.size(), ok};
}

// ------------------------- TinyGPSPlus -------------------------
Result run_tinygps(const std::vector<std::string> &lines, int iters) {
  double best = 1e30;
  size_t ok = 0;
  for (int it = 0; it < iters; ++it) {
    TinyGPSPlus gps;
    g_sink = 0;
    double t0 = now_sec();
    for (const auto &l : lines) {
      for (char c : l) gps.encode(c);
      gps.encode('\n'); // терминатор строки (в реальном потоке после кадра идёт \n)
    }
    double dt = now_sec() - t0;
    size_t cur = gps.passedChecksum(); // кол-во предложений с корректной CRC
    if (dt < best) { best = dt; ok = cur; }
  }
  return {best, lines.size(), ok};
}

static void print_result(const char *name, const Result &r) {
  double pps = r.lines / r.seconds;
  std::cout << "  " << name << ": " << r.seconds << "s, " << (long)pps
            << " lines/s, ok=" << r.ok << "/" << r.lines << " ("
            << (100.0 * r.ok / r.lines) << "%)\n";
}

int main(int argc, char **argv) {
  std::string path = (argc > 1) ? argv[1] : "";
  auto lines = load_lines(path);
  if (lines.empty()) {
    std::cerr << "No lines loaded.\n";
    return 1;
  }

  const int iters = 5;
  std::cout << "=== NMEA parser comparison ===\n";
  std::cout << "File: " << path << "\n";
  std::cout << "Lines: " << lines.size() << ", iterations: " << iters << "\n";
  std::cout << "Compiler: " << __VERSION__ << "\n\n";

  auto rl = run_lightnmea(lines, iters);
  auto rm = run_minmea(lines, iters);
  auto rt = run_tinygps(lines, iters);

  print_result("LightNMEA ", rl);
  print_result("minmea    ", rm);
  print_result("TinyGPS+  ", rt);

  double base = rl.lines / rl.seconds;
  auto ratio = [&](const Result &r) { return 100.0 * (r.lines / r.seconds) / base; };
  std::cout << "\n  Relative to LightNMEA: minmea " << ratio(rm)
            << "%, TinyGPS+ " << ratio(rt) << "%\n";
  std::cout << "  Sink: " << g_sink << "\n";
  return 0;
}