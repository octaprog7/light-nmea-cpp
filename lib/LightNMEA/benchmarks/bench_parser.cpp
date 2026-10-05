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
 * @file bench_parser.cpp
 * @brief Конфигурируемый бенчмарк пропускной способности NMEA-парсеров.
 *
 * Позволяет задать тип сентенций (RMC/GGA/VTG/GLL/ALL), процент повреждённых
 * пакетов (50% битый CRC + 50% обрыв), объём в тысячах пакетов, порядок выбора
 * (RND/SEQ) и реализацию парсера (LIGHT/MINMEA/TINYGPS/LIBNAMEA/ALL).
 * Сравниваемые реализации лежат в benchmarks/compare/third_party.
 *
 * Горячий контур: предзаполненный пул готовых строк + предгенерированный план
 * индексов (1 байт/пакет), разбитый на плотные последовательности здоровых и
 * испорченных пакетов - внутри таймера нет ни генерации текста, ни memcpy.
 */

#include "light_nmea.hpp"
#include "nav_gen.hpp"

// Конкуренты для сравнения (лежат в benchmarks/compare/third_party):
#include "minmea.h"
#include "TinyGPS++.h"
#include "nmea.h" // libnmea (jacketizer): статическая сборка, см. CMakeLists.txt

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace light_nmea;

// ============================================================================
// Тип сентенции для параметрического режима
// ============================================================================
/**
 * @brief Тип NMEA-сентенции для параметрического режима бенчмарка.
 *
 * RMC/GGA/VTG/GLL - конкретные типы; ALL - все поддерживаемые парсером типы
 * (неподдерживаемые, например GSV, в тестовый поток не включаются,
 * см. matches_type()).
 */
enum class SentenceType { RMC, GGA, VTG, GLL, ALL, INVALID };

/**
 * @brief Возвращает копию строки в верхнем регистре.
 * @param s Исходная строка (передаётся по значению - копируется).
 * @return Строка, преобразованная через std::toupper.
 */
static std::string to_upper_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

/**
 * @brief Человекочитаемое имя типа сентенции для вывода.
 * @param t Тип сентенции.
 * @return Строка-имя ("RMC", "GGA", "VTG", "GLL", "ALL" или "?").
 */
static const char *type_name(SentenceType t) {
  switch (t) {
    case SentenceType::RMC: return "RMC";
    case SentenceType::GGA: return "GGA";
    case SentenceType::VTG: return "VTG";
    case SentenceType::GLL: return "GLL";
    case SentenceType::ALL: return "ALL";
    default:               return "?";
  }
}

/**
 * @brief Преобразует строку CLI в тип сентенции (регистронезависимо).
 * @param s Строка типа ("RMC", "gga", ...).
 * @return SentenceType; INVALID для неизвестного значения.
 */
static SentenceType parse_type(const std::string &s) {
  const std::string u = to_upper_copy(s);
  if (u == "RMC") return SentenceType::RMC;
  if (u == "GGA") return SentenceType::GGA;
  if (u == "VTG") return SentenceType::VTG;
  if (u == "GLL") return SentenceType::GLL;
  if (u == "ALL") return SentenceType::ALL;
  return SentenceType::INVALID;
}

// ============================================================================
// Реализация парсера для сравнения
// ============================================================================
/**
 * @brief Реализация парсера для сравнения.
 *
 * LIGHT - LightNMEA (header-only, эталон), MINMEA - kosma/minmea (чистый C),
 * TINYGPS - mikalhart/TinyGPSPlus (побайтовый encode(), Arduino-стиль),
 * LIBNAMEA - jacketizer/libnmea (in-place токенизация + malloc/free на пакет),
 * ALL - прогон одного и того же плана данных через все реализации подряд.
 */
enum class BenchImpl { LIGHT, MINMEA, TINYGPS, LIBNAMEA, ALL };

/**
 * @brief Преобразует строку CLI в реализацию парсера (регистронезависимо).
 * @param s Имя реализации ("LIGHT"/"MINMEA"/"TINYGPS"/"LIBNAMEA"/"ALL").
 * @return BenchImpl; по умолчанию - LIGHT (в т.ч. "LN").
 */
static BenchImpl parse_impl(const std::string &s) {
  const std::string u = to_upper_copy(s);
  if (u == "MINMEA") return BenchImpl::MINMEA;
  if (u == "TINYGPS" || u == "TINY") return BenchImpl::TINYGPS;
  if (u == "LIBNAMEA" || u == "LIBNMEA" || u == "LNMEA") return BenchImpl::LIBNAMEA;
  if (u == "ALL") return BenchImpl::ALL;
  return BenchImpl::LIGHT; // по умолчанию (в т.ч. "LIGHT"/"LN")
}

/**
 * @brief Человекочитаемое имя реализации для вывода.
 * @param i Реализация парсера.
 * @return Строка-имя ("LIGHT", "MINMEA", "TINYGPS", "LIBNAMEA" или "ALL").
 */
static const char *impl_name(BenchImpl i) {
  switch (i) {
    case BenchImpl::LIGHT:    return "LIGHT";
    case BenchImpl::MINMEA:   return "MINMEA";
    case BenchImpl::TINYGPS:  return "TINYGPS";
    case BenchImpl::LIBNAMEA: return "LIBNAMEA";
    default:                  return "ALL";
  }
}

/**
 * @brief Адаптер minmea: разбор одной NULL-terminated NMEA-строки.
 *
 * Без CRC-валидации внутри (как задумано в minmea - CRC проверяется отдельно).
 * @param s Указатель на NUL-терминированную строку пакета.
 * @return true, если предложение распознано и поля извлечены.
 */
inline bool parse_minmea_line(const char *s) noexcept {
  const enum minmea_sentence_id id = minmea_sentence_id(s, false);
  switch (id) {
    case MINMEA_SENTENCE_RMC: {
      struct minmea_sentence_rmc f;
      return minmea_parse_rmc(&f, s);
    }
    case MINMEA_SENTENCE_GGA: {
      struct minmea_sentence_gga f;
      return minmea_parse_gga(&f, s);
    }
    case MINMEA_SENTENCE_VTG: {
      struct minmea_sentence_vtg f;
      return minmea_parse_vtg(&f, s);
    }
    case MINMEA_SENTENCE_GLL: {
      struct minmea_sentence_gll f;
      return minmea_parse_gll(&f, s);
    }
    default:
      return false;
  }
}

// ============================================================================
// Справка (аналог argparse)
// ============================================================================
/**
 * @brief Выводит справку CLI (аналог argparse).
 * @param prog Имя исполняемого файла (argv[0]) для примеров запуска.
 */
static void print_help(const char *prog) {
  std::cout <<
    "Usage: " << prog << " [OPTIONS] [TYPE] [ERRORS] [COUNT_K]\n"
    "\n"
    "Benchmark LightNMEA parser throughput on a configurable synthetic NMEA stream.\n"
    "\n"
    "Positional arguments:\n"
    "  TYPE        Sentence type: RMC, GGA, VTG, GLL or ALL (default: ALL)\n"
    "  ERRORS      Percent of corrupted packets: 0..100 (default: 0)\n"
    "              Corrupted = 50%% bad CRC + 50%% truncated sentences\n"
    "  COUNT_K     Number of sentences in thousands (default: 1000)\n"
    "\n"
    "Options:\n"
    "  -t, --type TYPE     same as positional TYPE\n"
    "  -e, --errors N      same as positional ERRORS\n"
    "  -n, --count N       same as positional COUNT_K\n"
    "  -o, --order MODE    packet selection: RND (random) or SEQ (sequential)\n"
    "                      (default: RND)\n"
    "  -i, --impl MODE     parser implementation: LIGHT, MINMEA, TINYGPS, LIBNAMEA or ALL\n"
    "                      (default: LIGHT; requires third_party libs for competitors)\n"
    "  -f, --file PATH     benchmark a real .nmea file instead of synthetic data\n"
    "  -h, --help          show this help and exit\n"
    "\n"
    "Examples:\n"
    "  " << prog << "                        # 1M ALL sentences, 0%% errors\n"
    "  " << prog << " RMC 5 200              # 200k RMC, 5%% corrupted\n"
    "  " << prog << " -t GGA -e 10 -n 50     # 50k GGA, 10%% corrupted\n"
    "  " << prog << " ALL 10 100000 SEQ      # 100M ALL, 10%% errors, sequential order\n"
    "  " << prog << " ALL 10 100 ALL         # 100k ALL: LIGHT vs MINMEA vs TINYGPS vs LIBNAMEA\n"
    "  " << prog << " --file trace.nmea      # real-file benchmark\n";
}

// ============================================================================
// Генератор уникальных NMEA-пакетов (для классических сценариев)
// ============================================================================

/**
 * @brief Генератор уникальных NMEA-пакетов для классических сценариев.
 *
 * Каждый вызов generate_rmc()/generate_gga() сдвигает координаты на 0.0001,
 * поэтому пакеты отличаются друг от друга (набор не повторяется). Используется
 * только в legacy_full_suite(); параметрический режим работает на готовых
 * строках из nav_gen::NAV_PACKETS.
 */
class NMEAGenerator {
public:
  /** @brief Конструктор: seed ГПСЧ и стартовые координаты (0; 0). */
  NMEAGenerator(uint32_t seed = 42) : rng_(seed), lat_(0), lon_(0) {}

  /**
   * @brief Сформировать уникальную корректную RMC-сентенцию с посчитанным CRC.
   * @return Готовая NMEA-строка с '*XX'-контрольной суммой.
   */
  std::string generate_rmc() {
    update_coordinates();

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
             "$GPRMC,%06u,A,%09.4f,N,%010.4f,E,%05.1f,%05.1f,230394,,,",
             static_cast<unsigned int>(123519 + (lat_ * 100)), 48.0 + lat_,
             11.0 + lon_, 22.4 + (lon_ * 0.1), 84.4);

    return finalize_packet(buf, len);
  }

  /**
   * @brief Сформировать уникальную корректную GGA-сентенцию с посчитанным CRC.
   * @return Готовая NMEA-строка с '*XX'-контрольной суммой.
   */
  std::string generate_gga() {
    update_coordinates();

    int sats = 6 + (rng_() % 8);

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
             "$GPGGA,%06u,%09.4f,N,%010.4f,E,1,%02d,1.2,%06.1f,M,47.0,M,,",
             static_cast<unsigned int>(123519 + (lat_ * 100)), 48.0 + lat_,
             11.0 + lon_, sats, 545.4 + (lon_ * 0.1));

    return finalize_packet(buf, len);
  }

  /**
   * @brief Сформировать заведомо невалидный пакет (CRC *FF не совпадёт с XOR).
   * @return Строка с некорректной контрольной суммой.
   */
  std::string generate_invalid() {
    return "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*FF";
  }

private:
  /** @brief Сдвигает координаты на шаг 0.0001 с циклическим возвратом. */
  void update_coordinates() {
    lat_ += 0.0001;
    lon_ += 0.0001;
    if (lat_ > 90) lat_ = 0;
    if (lon_ > 180) lon_ = 0;
  }

  /**
   * @brief Дописывает к телу пакета '*' + CRC-0183 (XOR байтов после '$').
   * @return Полная NMEA-строка с контрольной суммой.
   */
  std::string finalize_packet(const char* buf, int len) {
    uint8_t crc = 0;
    for (int i = 1; i < len; ++i) {
      crc ^= static_cast<uint8_t>(buf[i]);
    }

    std::string result(buf, len);
    result += '*';
    result += "0123456789ABCDEF"[(crc >> 4) & 0x0F];
    result += "0123456789ABCDEF"[crc & 0x0F];
    return result;
  }

  std::mt19937 rng_;
  double lat_, lon_;
};

/**
 * @brief "Сток" результатов разбора в горячем цикле.
 *
 * volatile int64_t: компилятор не может выкинуть парсинг как мёртвый код -
 * результат всегда накапливается в этой переменной (без float/double, чтобы
 * не добавлять накладных расходов на преобразования).
 */
volatile int64_t g_sink = 0;

/**
 * @brief Прогон набора готовых пакетов через LightNMEA (классический режим).
 *
 * @param name       Название сценария для вывода.
 * @param packets    Вектор готовых NMEA-строк.
 * @param iterations Количество повторных замеров (усреднение скорости).
 */
void benchmark_packets(const std::string &name,
                       const std::vector<std::string> &packets,
                       int iterations = 3) {
  std::cout << "\n=== " << name << " ===\n";
  std::cout << "Packets: " << packets.size() << ", Iterations: " << iterations << "\n";

  std::vector<double> speeds;
  size_t total_success = 0;

  for (int iter = 0; iter < iterations; ++iter) {
    Parser parser;
    g_sink = 0;

    auto start = std::chrono::high_resolution_clock::now();

    size_t success = 0;
    for (const auto &packet : packets) {
      if (parser.parse_line(packet)) {
        success++;
        // новые strong type поля .raw
        g_sink += parser.data().lat.raw;
        g_sink += parser.data().lon.raw;
        g_sink += parser.data().speed_kn_x1000.raw;
      }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double seconds = duration.count() / 1'000'000.0;
    double speed = packets.size() / seconds;
    speeds.push_back(speed);
    total_success = success;

    std::cout << "  Iter " << (iter + 1) << ": " << std::fixed
              << std::setprecision(4) << seconds << "s, "
              << static_cast<int>(speed) << " pkt/s, "
              << "success: " << success << "/" << packets.size() << "\n";
    std::cout << "  Sink value: " << g_sink << "\n";
  }

  double avg = std::accumulate(speeds.begin(), speeds.end(), 0.0) / speeds.size();
  std::cout << "  Average: " << static_cast<int>(avg) << " pkt/s\n";
  std::cout << "  Success rate: " << std::fixed << std::setprecision(2)
            << (100.0 * total_success / packets.size()) << "%\n";
}

/**
 * @brief Загружает реальный .nmea-файл в память (по одной строке на пакет).
 *
 * Убирает хвостовой '\r' (Windows-переводы строк); пустые строки пропускает.
 * @return Вектор строк-пакетов; пустой вектор, если файл не открылся.
 */
std::vector<std::string> load_nmea_file(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: cannot open file " << filename << "\n";
    return {};
  }

  std::vector<std::string> packets;
  std::string line;
  size_t total_lines = 0;
  size_t empty_lines = 0;

  while (std::getline(file, line)) {
    total_lines++;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      empty_lines++;
      continue;
    }
    packets.push_back(line);
  }

  std::cout << "  Loaded " << packets.size() << " packets from " << total_lines
            << " lines (" << empty_lines << " empty)\n";

  return packets;
}

// ============================================================================
// Параметрическая генерация: тип + процент ошибок + количество
// ============================================================================

/**
 * @brief Соответствует ли пакет выбранному типу сентенции.
 *
 * ALL = только поддерживаемые парсером типы (RMC, GGA, VTG, GLL);
 * неподдерживаемые (GSV и пр.) в тестовый поток не включаются.
 * @return true, если пакет следует включить в пул.
 */
static bool matches_type(std::string_view p, SentenceType t) {
  if (p.size() < 6 || p[0] != '$') return false;
  const char c3 = p[3], c4 = p[4], c5 = p[5];
  switch (t) {
    case SentenceType::RMC: return c3 == 'R' && c4 == 'M' && c5 == 'C';
    case SentenceType::GGA: return c3 == 'G' && c4 == 'G' && c5 == 'A';
    case SentenceType::VTG: return c3 == 'V' && c4 == 'T' && c5 == 'G';
    case SentenceType::GLL: return c3 == 'G' && c4 == 'L' && c5 == 'L';
    case SentenceType::ALL:
      return (c3 == 'R' && c4 == 'M' && c5 == 'C') ||
             (c3 == 'G' && c4 == 'G' && c5 == 'A') ||
             (c3 == 'V' && c4 == 'T' && c5 == 'G') ||
             (c3 == 'G' && c4 == 'L' && c5 == 'L');
    default: return false;
  }
}

/**
 * @brief Элемент небольшого пула готовых строк.
 *
 * Память O(pool), а не O(count): здоровые пакеты в горячем цикле выбираются
 * случайным индексом из этого компактного массива.
 */
struct PoolEntry {
  std::string_view text; // готовая строка из nav_gen::NAV_PACKETS
  char type;             // 'R','G','V','L','O' - для статистики
  size_t star;           // позиция '*' (или len, если '*' нет)
};

/**
 * @brief Строит пул готовых строк нужного типа.
 * @param t Тип сентенций (ALL => все поддерживаемые шаблоны nav_gen).
 * @return Пул строк с метаданными (тип для статистики, позиция '*').
 */
static std::vector<PoolEntry> make_pool(SentenceType t) {
  std::vector<PoolEntry> pool;
  for (const auto p : nav_gen::NAV_PACKETS) {
    if (!matches_type(p, t)) continue;
    char ty = 'O';
    if (p.size() >= 6 && p[0] == '$') {
      if (p[3] == 'R' && p[4] == 'M' && p[5] == 'C') ty = 'R';
      else if (p[3] == 'G' && p[4] == 'G' && p[5] == 'A') ty = 'G';
      else if (p[3] == 'V' && p[4] == 'T' && p[5] == 'G') ty = 'V';
      else if (p[3] == 'G' && p[4] == 'L' && p[5] == 'L') ty = 'L';
    }
    const size_t star = p.find('*');
    pool.push_back({p, ty, star == std::string_view::npos ? p.size() : star});
  }
  if (pool.empty()) { // страховка
    pool.push_back({nav_gen::NAV_PACKETS[0], 'O', nav_gen::NAV_PACKETS[0].size()});
  }
  return pool;
}

/**
 * @brief xorshift64 - быстрый ГПСЧ для индексов и решений об ошибке.
 * @param s Состояние генератора (изменяется на каждом вызове).
 * @return Очередное 64-битное псевдослучайное значение.
 */
static inline uint64_t next_rand(uint64_t &s) noexcept {
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return s;
}

/**
 * @brief Вид повреждения пакета для генератора.
 *
 * None - корректный пакет; CrcBad - битая контрольная сумма (флип hex-цифры);
 * Truncated - обрыв прямо перед '*' (нет ни CRC, ни окончания \r\n).
 */
enum class CorruptKind : uint8_t { None, CrcBad, Truncated };

/**
 * @brief Генерирует одну сентенцию случайного типа из пула в переданный буфер.
 *
 * Применяет указанный вид повреждения (None/CrcBad/Truncated).
 * @return Длина записанной сентенции (0 - переполнение буфера).
 * @param out_type [out] Тип выбранной сентенции ('R','G','V','L','O') для статистики.
 */
static size_t gen_sentence(const std::vector<PoolEntry> &pool, CorruptKind kind,
                           uint64_t &rng, char *buf, size_t cap,
                           unsigned char *out_type) noexcept {
  const PoolEntry &e = pool[next_rand(rng) % pool.size()];
  const size_t len = e.text.size();
  if (len > cap) return 0;
  if (out_type) *out_type = static_cast<unsigned char>(e.type);
  std::memcpy(buf, e.text.data(), len);
  switch (kind) {
    case CorruptKind::None:
      return len;
    case CorruptKind::CrcBad:
      if (e.star + 1 < len) buf[e.star + 1] = (buf[e.star + 1] == '0') ? '1' : '0';
      return len;
    case CorruptKind::Truncated:
      return e.star; // обрыв прямо перед '*' (без контрольной суммы)
  }
  return len;
}

/**
 * @brief Предзаполненный компактный буфер испорченных сентенций.
 *
 * Память O(n*~80). Заполняется ДО запуска таймера - в горячем цикле остаётся
 * только выбор случайного индекса. Каждая запись NUL-терминирована (minmea).
 */
struct CorruptPool {
  std::vector<char> data;
  std::vector<size_t> offs;            // [n+1]
  std::vector<unsigned char> type;     // 'R','G','V','L','O'
  std::vector<unsigned char> is_trunc; // 1 = оборванная

  size_t count() const { return offs.empty() ? 0 : offs.size() - 1; }
  std::string_view at(size_t k) const {
    return {data.data() + offs[k], offs[k + 1] - offs[k]};
  }
};

/**
 * @brief Плоский элемент испорченной сентенции.
 *
 * Убирает двойную индирекцию (offs[k], offs[k+1]) из горячего цикла -
 * один компактный, кэш-дружелюбный struct.
 */
struct CorruptEntry {
  const char *d;
  uint16_t len;
  uint8_t type;  // 'R','G','V','L','O'
  uint8_t trunc; // 1 = оборванная
};

/**
 * @brief Предзаполняет буфер испорченных сентенций (до запуска таймера).
 *
 * Чередует CrcBad и Truncated; размер округляется до степени двойки - в горячем
 * цикле используется маска вместо деления.
 * @param pool Пул здоровых строк-источников.
 * @param n    Желаемое число вариантов (уже ограничено CORRUPT_POOL_MAX).
 * @param seed Начальное состояние ГПСЧ.
 */
static CorruptPool build_corrupt_pool(const std::vector<PoolEntry> &pool,
                                      size_t n, uint64_t seed) {
  CorruptPool cp;
  cp.offs.push_back(0);
  if (n == 0) return cp;
  // Дополняем до степени двойки: в горячем цикле маска вместо деления.
  // Минимум 64 записи - план использует фиксированную маску 0x3F (0..63),
  // при меньшем np индексы выходили бы за пределы массива.
  size_t np = 64;
  while (np < n) np <<= 1;
  cp.data.reserve(np * 96);
  cp.offs.reserve(np + 1);
  cp.type.reserve(np);
  cp.is_trunc.reserve(np);

  uint64_t rng = seed;
  char buf[192];
  for (size_t i = 0; i < np; ++i) {
    const CorruptKind kind = (i & 1) ? CorruptKind::Truncated : CorruptKind::CrcBad;
    unsigned char ty = 'O';
    const size_t len = gen_sentence(pool, kind, rng, buf, sizeof(buf), &ty);
    cp.data.insert(cp.data.end(), buf, buf + len);
    cp.offs.push_back(cp.data.size()); // длина = len (без '\0')
    cp.data.push_back('\0');           // NUL-терминация для minmea
    cp.type.push_back(ty);
    cp.is_trunc.push_back(kind == CorruptKind::Truncated ? 1u : 0u);
  }
  return cp;
}

/**
 * @brief Индекс типа для статистики: R=0, G=1, V=2, L=3, Other=4.
 * @param t Буква типа ('R','G','V','L' или что-то ещё).
 * @return Индекс в массиве распределения dist_total[5].
 */
static inline int type_index(char t) noexcept {
  switch (t) {
    case 'R': return 0;
    case 'G': return 1;
    case 'V': return 2;
    case 'L': return 3;
    default:  return 4;
  }
}

/**
 * @brief Основной таймируемый прогон: один план данных через выбранный парсер.
 *
 * Горячий цикл: здоровые сентенции берутся из пула по индексу плана,
 * испорченные - из предзаполненного буфера. Внутри таймера нет ни генерации
 * текста, ни memcpy (кроме libnmea, которому нужен изменяемый scratch-буфер).
 * Измеряется пропускная способность в pkt/s по всем пакетам плана.
 *
 * @param name       Заголовок секции вывода.
 * @param pool       Пул здоровых готовых строк.
 * @param err_pct    Процент повреждённых пакетов 0..100.
 * @param count      Общее число пакетов в прогоне.
 * @param iterations Число повторных замеров (усреднение).
 * @param sequential true - SEQ-план, false - RND-план.
 * @param impl       Реализация парсера (LIGHT/MINMEA/TINYGPS/LIBNAMEA).
 */
static void benchmark_stream(const std::string &name,
                             const std::vector<PoolEntry> &pool,
                             int err_pct, size_t count, int iterations,
                             bool sequential, BenchImpl impl) {
  // Буфер испорченных сентенций: не больше CORRUPT_POOL_MAX вариантов.
  // Больший размер приводит к промахам кэша при случайном доступе к данным
  // за пределами кэша; выбор с возвращением из небольшого пула статистически
  // эквивалентен, но остаётся в L1/L2.
  constexpr size_t CORRUPT_POOL_MAX = 64;
  size_t corrupt_n =
      static_cast<size_t>(count) * static_cast<unsigned>(err_pct) / 100;
  if (err_pct > 0 && corrupt_n == 0) corrupt_n = 1;
  const size_t corrupt_pool_size = std::min<size_t>(corrupt_n, CORRUPT_POOL_MAX);
  const CorruptPool cp = build_corrupt_pool(pool, corrupt_pool_size, 0xC0FFEEull);

  // Здоровый пул дополняем до степени двойки.
  // Циклическое заполнение сохраняет пропорции типов в распределении.
  // Минимум 32 записи: план использует фиксированную маску 0x1F (0..31),
  // при меньшем psize индексы выходили бы за пределы pool_p2.
  const size_t psize_orig = pool.size();
  size_t psize = 32;
  while (psize < psize_orig) psize <<= 1;
  std::vector<PoolEntry> pool_p2(psize);
  for (size_t i = 0; i < psize; ++i) pool_p2[i] = pool[i % psize_orig];

  // Порог ошибок в шкале 16 бит: (next_rand >> 48) < thr  <=>  err_pct%
  const uint16_t thr = static_cast<uint16_t>(
      static_cast<uint32_t>(err_pct) * 65535u / 100u);

  // Предгенерируем план выбора пакетов (1 байт на пакет) ДО замера:
  // bit7 = "испорчен", bits 0..5 = индекс (здоровый <=31, испорченный <=63).
  // SEQ: предсказуемый циклический обход пулов + регулярный паттерн ошибок
  //      (каждый i с (i % 100) < err_pct) - максимум для префетчера и веток.
  // RND: случайные индексы через ГПСЧ (реалистичный "шумный" поток).
  std::vector<uint8_t> plan(count);
  if (sequential) {
    size_t hi = 0, ci = 0;
    for (size_t i = 0; i < count; ++i) {
      const bool bad = (i % 100u) < static_cast<unsigned>(err_pct);
      plan[i] = bad ? static_cast<uint8_t>(0x80u | (ci++ & 0x3Fu))
                    : static_cast<uint8_t>(hi++ & 0x1Fu);
    }
  } else {
    uint64_t rng = 0x9E3779B97F4A7C15ull ^ 0xC0FFEEull;
    for (size_t i = 0; i < count; ++i) {
      const bool bad = static_cast<uint16_t>(next_rand(rng) >> 48) < thr;
      plan[i] = bad ? static_cast<uint8_t>(0x80u | (next_rand(rng) & 0x3Fu))
                    : static_cast<uint8_t>(next_rand(rng) & 0x1Fu);
    }
  }

  // Плоская таблица испорченных сентенций: один компактный struct вместо
  // двойной индирекции cp.at(k) (offs[k], offs[k+1]).
  std::vector<CorruptEntry> corrupt_entries(cp.count());
  for (size_t k = 0; k < corrupt_entries.size(); ++k) {
    const std::string_view sv = cp.at(k);
    corrupt_entries[k] = {sv.data(), static_cast<uint16_t>(sv.size()),
                          cp.type[k], cp.is_trunc[k]};
  }

  // Разбиение плана на две плотные последовательности (здоровые/испорченные).
  // Убирает случайную ветку "bad" из таймируемого цикла: каждый проход
  // однороден, прогноз ветвлений идеален.
  std::vector<uint8_t> plan_ok, plan_bad;
  plan_ok.reserve(count);
  plan_bad.reserve(count);
  for (uint8_t p : plan) {
    if (p & 0x80u) plan_bad.push_back(p);
    else plan_ok.push_back(p);
  }

  // Вся статистика, не зависящая от парсера, вычисляется ДО таймера:
  // распределение типов, число повреждённых и оборванных - по плану.
  const size_t corrupt_total = plan_bad.size();
  size_t broken_total = 0;
  std::array<size_t, 5> dist_total{}; // R, G, V, L, Other
  for (uint8_t p : plan_ok) {
    ++dist_total[type_index(pool_p2[p & 0x1Fu].type)];
  }
  for (uint8_t p : plan_bad) {
    const CorruptEntry &ce = corrupt_entries[p & 0x3Fu];
    if (ce.trunc) ++broken_total;
    else ++dist_total[type_index(static_cast<char>(ce.type))];
  }

  std::cout << "\n=== " << name << " ===\n";
  std::cout << "Packets: " << count << ", Iterations: " << iterations
            << ", pool: " << pool.size() << " ready strings, "
            << "corrupt buffer: " << corrupt_entries.size()
            << ", order: " << (sequential ? "SEQ" : "RND")
            << ", plan: " << plan.size() << " bytes (ok=" << plan_ok.size()
            << ", bad=" << plan_bad.size() << ")\n";

  std::vector<double> speeds;
  size_t last_success = 0, last_corrupt = 0, last_broken = 0;
  std::array<size_t, 5> last_dist{}; // R, G, V, L, Other

  for (int iter = 0; iter < iterations; ++iter) {
    g_sink = 0;
    size_t success = 0;

    const auto t0 = std::chrono::high_resolution_clock::now();

    if (impl == BenchImpl::TINYGPS) {
      // Побайтовый потоковый парсер: encode(char) на каждый байт + '\n'.
      TinyGPSPlus gps;
      for (size_t n = 0; n < plan_ok.size(); ++n) {
        const PoolEntry &e = pool_p2[plan_ok[n] & 0x1Fu];
        for (char c : e.text) gps.encode(c);
        gps.encode('\n');
      }
      for (size_t n = 0; n < plan_bad.size(); ++n) {
        const CorruptEntry &ce = corrupt_entries[plan_bad[n] & 0x3Fu];
        for (size_t j = 0; j < ce.len; ++j) gps.encode(ce.d[j]);
        gps.encode('\n');
      }
      success = gps.passedChecksum(); // кол-во предложений с корректной CRC
      g_sink = static_cast<int64_t>(success);
    } else if (impl == BenchImpl::MINMEA) {
      // minmea: построчный разбор NULL-terminated строк (без CRC внутри).
      for (size_t n = 0; n < plan_ok.size(); ++n) {
        const PoolEntry &e = pool_p2[plan_ok[n] & 0x1Fu];
        if (parse_minmea_line(e.text.data())) ++success;
      }
      for (size_t n = 0; n < plan_bad.size(); ++n) {
        const CorruptEntry &ce = corrupt_entries[plan_bad[n] & 0x3Fu];
        if (parse_minmea_line(ce.d)) ++success;
      }
      g_sink = static_cast<int64_t>(success);
    } else if (impl == BenchImpl::LIBNAMEA) {
      // libnmea: мутирует входной буфер на месте (токенизация через '\0'),
      // на каждый пакет malloc/free (nmea_parse/nmea_free).
      // Копируем сентенцию в стековый scratch: валидность требует \r\n и <=82.
      char scratch[96];
      for (size_t n = 0; n < plan_ok.size(); ++n) {
        const PoolEntry &e = pool_p2[plan_ok[n] & 0x1Fu];
        const size_t len = e.text.size();
        std::memcpy(scratch, e.text.data(), len);
        nmea_s *d = nmea_parse(scratch, len, 1);
        if (d != nullptr) {
          ++success;
          g_sink += static_cast<int64_t>(d->type);
        }
        nmea_free(d);
      }
      for (size_t n = 0; n < plan_bad.size(); ++n) {
        const CorruptEntry &ce = corrupt_entries[plan_bad[n] & 0x3Fu];
        std::memcpy(scratch, ce.d, ce.len);
        nmea_s *d = nmea_parse(scratch, ce.len, 1);
        if (d != nullptr) {
          ++success;
          g_sink += static_cast<int64_t>(d->type);
        }
        nmea_free(d);
      }
    } else {
      // LightNMEA (по умолчанию): построчный разбор string_view.
      Parser parser;
      for (size_t n = 0; n < plan_ok.size(); ++n) {
        const PoolEntry &e = pool_p2[plan_ok[n] & 0x1Fu];
        if (parser.parse_line(e.text)) {
          ++success;
          g_sink += parser.data().lat.raw;
        }
      }
      for (size_t n = 0; n < plan_bad.size(); ++n) {
        const CorruptEntry &ce = corrupt_entries[plan_bad[n] & 0x3Fu];
        if (parser.parse_line(std::string_view(ce.d, ce.len))) {
          ++success;
          g_sink += parser.data().lat.raw;
        }
      }
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    const double speed = count / seconds;
    speeds.push_back(speed);
    last_success = success;
    last_corrupt = corrupt_total;
    last_broken = broken_total;
    last_dist = dist_total;

    std::cout << "  Iter " << (iter + 1) << ": " << std::fixed
              << std::setprecision(4) << seconds << "s, "
              << static_cast<int>(speed) << " pkt/s, "
              << "success: " << success << "/" << count << "\n";
    std::cout << "  Sink value: " << g_sink << "\n";
  }

  const double avg = std::accumulate(speeds.begin(), speeds.end(), 0.0) / speeds.size();
  std::cout << "  Average: " << static_cast<int>(avg) << " pkt/s\n";
  std::cout << "  Success rate: " << std::fixed << std::setprecision(2)
            << (100.0 * last_success / count) << "%\n";
  std::cout << "  Corrupted: " << last_corrupt << " ("
            << std::setprecision(2) << (100.0 * last_corrupt / count) << "%)\n";
  std::cout << "  Distribution: RMC=" << last_dist[0] << " GGA=" << last_dist[1]
            << " VTG=" << last_dist[2] << " GLL=" << last_dist[3]
            << " Other=" << last_dist[4] << " Broken=" << last_broken << "\n";
}

/**
 * @brief Печатает статистику по типам сентенций в наборе.
 * @param packets Вектор строк-пакетов (реальные данные, режим --file).
 */
static void print_distribution(const std::vector<std::string> &packets) {
  size_t rmc = 0, gga = 0, vtg = 0, gll = 0, other = 0, broken = 0;
  for (const auto &p : packets) {
    if (p.size() >= 6 && p[0] == '$' && p.find('*') != std::string::npos) {
      if (p[3] == 'R' && p[4] == 'M' && p[5] == 'C') rmc++;
      else if (p[3] == 'G' && p[4] == 'G' && p[5] == 'A') gga++;
      else if (p[3] == 'V' && p[4] == 'T' && p[5] == 'G') vtg++;
      else if (p[3] == 'G' && p[4] == 'L' && p[5] == 'L') gll++;
      else other++;
    } else {
      broken++;
    }
  }
  std::cout << "  Distribution: RMC=" << rmc << " GGA=" << gga
            << " VTG=" << vtg << " GLL=" << gll
            << " Other=" << other << " Broken=" << broken << "\n";
}

// ============================================================================
// Режимы запуска
// ============================================================================

/**
 * @brief Режим бенчмарка на реальном .nmea-файле (только LightNMEA).
 * @param filename Путь к файлу с NMEA-строками.
 * @return Код возврата (0 - успех).
 */
static int file_mode(const std::string &filename) {
  std::cout << "\n[FILE MODE] Reading: " << filename << "\n";

  auto packets = load_nmea_file(filename);
  if (packets.empty()) {
    std::cerr << "Error: no packets loaded\n";
    return 1;
  }

  benchmark_packets("Real data: " + filename, packets, 5);
  print_distribution(packets);
  return 0;
}

/**
 * @brief Классический прогон всех сценариев (без аргументов CLI).
 *
 * Пять сценариев по 1M пакетов: уникальные RMC, уникальные GGA, смесь RMC/GGA,
 * смесь с невалидными CRC и разнородный набор nav_gen (с распределением типов).
 * @return 0 - всегда.
 */
static int legacy_full_suite() {
  std::cout << "\n[SYNTHETIC MODE] Generating test data...\n";
  NMEAGenerator gen;

  {
    std::vector<std::string> packets;
    packets.reserve(1'000'000);
    for (int i = 0; i < 1'000'000; ++i) packets.push_back(gen.generate_rmc());
    benchmark_packets("Scenario 1: 1M unique RMC", packets);
  }

  {
    std::vector<std::string> packets;
    packets.reserve(1'000'000);
    for (int i = 0; i < 1'000'000; ++i) packets.push_back(gen.generate_gga());
    benchmark_packets("Scenario 2: 1M unique GGA", packets);
  }

  {
    std::vector<std::string> packets;
    packets.reserve(1'000'000);
    for (int i = 0; i < 1'000'000; ++i) {
      packets.push_back(i % 2 == 0 ? gen.generate_rmc() : gen.generate_gga());
    }
    benchmark_packets("Scenario 3: 1M mixed RMC/GGA", packets);
  }

  {
    std::vector<std::string> packets;
    packets.reserve(1'000'000);
    for (int i = 0; i < 1'000'000; ++i) {
      packets.push_back(i % 2 == 0 ? gen.generate_rmc() : gen.generate_invalid());
    }
    benchmark_packets("Scenario 4: 1M mixed (50% invalid CRC)", packets);
  }

  {
    std::cout << "\n[NAV_GEN MODE] Testing with diverse packet set...\n";

    std::vector<std::string> packets;
    packets.reserve(1'000'000);
    for (int i = 0; i < 1'000'000; ++i) {
      packets.emplace_back(nav_gen::get_nav_packet());
    }

    benchmark_packets("Scenario 5: 1M diverse packets (nav_gen)", packets, 3);
    print_distribution(packets);
  }

  return 0;
}

/**
 * @brief Параметрический режим: тип + % ошибок + объём + порядок + реализация.
 *
 * Для impl == ALL один и тот же план и данные прогоняются через все четыре
 * реализации (LIGHT, MINMEA, TINYGPS, LIBNAMEA) - честное сравнение на
 * идентичном входном потоке.
 * @return Код возврата (0 - успех).
 */
static int parametric_mode(SentenceType type, int err_pct, size_t count,
                           bool sequential, BenchImpl impl) {
  auto pool = make_pool(type);

  std::cout << "\n[PARAMETRIC MODE] Type=" << type_name(type)
            << ", errors=" << err_pct << "%, count=" << count
            << ", order=" << (sequential ? "SEQ" : "RND")
            << ", impl=" << impl_name(impl)
            << ", pool=" << pool.size() << " ready strings\n";

  const std::string base =
      std::string("Parametric: ") + type_name(type) + ", " +
      std::to_string(err_pct) + "% errors, " + std::to_string(count) + " pkt";

  if (impl == BenchImpl::ALL) {
    // Тот же план и данные прогоняются через все четыре реализации.
    for (const BenchImpl i : {BenchImpl::LIGHT, BenchImpl::MINMEA, BenchImpl::TINYGPS,
                              BenchImpl::LIBNAMEA}) {
      benchmark_stream(base + " [" + impl_name(i) + "]", pool, err_pct, count,
                       3, sequential, i);
    }
  } else {
    benchmark_stream(base + " [" + impl_name(impl) + "]", pool, err_pct, count,
                     3, sequential, impl);
  }
  return 0;
}

/**
 * @brief Точка входа: разбор аргументов (argparse-подобный CLI) и запуск режима.
 *
 * Без аргументов - legacy_full_suite(); с параметрами - parametric_mode()
 * (или file_mode при --file). Поддерживаются флаги -t/-e/-n/-o/-i/-f и
 * позиционные [TYPE] [ERRORS] [COUNT_K] [ORDER] [IMPL].
 * @return Код возврата процесса.
 */
int main(int argc, char *argv[]) {
  std::cout << "=== LightNMEA Extended Benchmark ===\n";
  std::cout << "Compiler: " << __VERSION__ << "\n";
// Честная метка оптимизации. Точные флаги передаёт скрипт сборки через
// -DBENCH_OPT_STR="..."; иначе - только факт наличия оптимизации, т.к.
// макрос __OPTIMIZE__ у GCC/Clang всегда равен 1 и уровень не раскрывает.
#if defined(BENCH_OPT_STR)
  std::cout << "Optimization: " << BENCH_OPT_STR;
#elif defined(__OPTIMIZE_SIZE__)
  std::cout << "Optimization: -Os (size)";
#elif defined(__OPTIMIZE__)
  std::cout << "Optimization: optimized (-O > 0)";
#else
  std::cout << "Optimization: -O0 (none)";
#endif
#ifdef NDEBUG
  std::cout << " [Release]\n";
#else
  std::cout << " [Debug]\n";
#endif

  // ---- разбор аргументов ----
  std::string type_s = "ALL";
  int err_pct = 0;
  int count_k = 1000;
  std::string order_s = "RND";
  std::string impl_s = "LIGHT";
  std::string file_path;
  bool have_params = false;
  std::vector<std::string> pos;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help" || a == "-help") {
      print_help(argv[0]);
      return 0;
    } else if (a == "-t" || a == "--type") {
      if (i + 1 < argc) { type_s = argv[++i]; have_params = true; }
    } else if (a == "-e" || a == "--errors") {
      if (i + 1 < argc) { err_pct = std::atoi(argv[++i]); have_params = true; }
    } else if (a == "-n" || a == "--count") {
      if (i + 1 < argc) { count_k = std::atoi(argv[++i]); have_params = true; }
    } else if (a == "-o" || a == "--order") {
      if (i + 1 < argc) { order_s = argv[++i]; have_params = true; }
    } else if (a == "-i" || a == "--impl") {
      if (i + 1 < argc) { impl_s = argv[++i]; have_params = true; }
    } else if (a == "-f" || a == "--file") {
      if (i + 1 < argc) { file_path = argv[++i]; have_params = true; }
    } else {
      pos.push_back(a);
    }
  }

  // Позиционные: [TYPE] [ERRORS] [COUNT_K] [ORDER] [IMPL], либо путь к файлу
  if (!pos.empty() && file_path.empty()) {
    const std::string p0 = to_upper_copy(pos[0]);
    if (parse_type(p0) != SentenceType::INVALID) {
      type_s = p0;
      have_params = true;
      if (pos.size() >= 2) err_pct = std::atoi(pos[1].c_str());
      if (pos.size() >= 3) count_k = std::atoi(pos[2].c_str());
      if (pos.size() >= 4) order_s = to_upper_copy(pos[3]);
      if (pos.size() >= 5) impl_s = to_upper_copy(pos[4]);
    } else {
      file_path = pos[0];
    }
  }

  // ---- режимы ----
  if (!file_path.empty()) {
    return file_mode(file_path);
  }

  const SentenceType type = parse_type(type_s);
  if (type == SentenceType::INVALID) {
    std::cerr << "Error: unknown sentence type '" << type_s << "'\n";
    print_help(argv[0]);
    return 2;
  }
  err_pct = std::max(0, std::min(100, err_pct));
  count_k = std::max(1, count_k);

  const bool sequential = (to_upper_copy(order_s) == "SEQ");
  const BenchImpl impl = parse_impl(impl_s);

  if (!have_params) {
    return legacy_full_suite(); // без аргументов - классический прогон всех сценариев
  }

  return parametric_mode(type, err_pct, static_cast<size_t>(count_k) * 1000,
                         sequential, impl);
}