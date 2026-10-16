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
 * @file fuzz_parser.cpp
 * @brief Фаззинг-тест парсера LightNMEA.
 *
 * Проверяет устойчивость парсера к повреждённым, оборванным и мусорным
 * пакетам/сентенциям. Предназначен для запуска ИЗ ПОД САНИТАЙЗЕРОВ:
 *   g++ -std=c++17 -O1 -g -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer -I../include fuzz_parser.cpp -o fuzz_parser
 *
 * Две фазы:
 *   1. Детерминированная - для каждого эталонного пакета из nav_gen
 *      перебираются все позиции: флип байта, обрыв, удаление, вставка,
 *      мусорный префикс/хвост, переполнение длины.
 *   2. Случайная - N случайных изменений (аргумент CLI, по умолчанию 1'000'000).
 *
 * Код возврата: 0 - все инварианты выполнены; 1 - найден FAIL.
 * Ошибки памяти / UB ловятся санитайзерами (ненулевой код).
 *
 * Запуск всего набора проверок: scripts/run_quality_checks.sh
 */

#include "light_nmea.hpp"
#include "nav_gen.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using namespace light_nmea;

namespace {

// ---------------------------------------------------------------------------
// ГПСЧ и вспомогательные утилиты
// ---------------------------------------------------------------------------

/** xorshift64 - быстрый детерминированный ГПСЧ для изменений. */
inline uint64_t next_rand(uint64_t &s) noexcept {
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return s;
}

inline uint8_t rnd_byte(uint64_t &s) noexcept {
  return static_cast<uint8_t>(next_rand(s));
}

/** Печать FAIL и установка флага ошибки (один раз на тип). */
int g_fail = 0;
void fail(const char *what) {
  if (!g_fail) std::printf("FAIL: %s\n", what);
  g_fail = 1;
}

// ---------------------------------------------------------------------------
// Подварианты распарсенных данных
// ---------------------------------------------------------------------------

/**
 * Проверяет данные после успешного вызова parse_line().
 * Критические: координаты в допустимых диапазонах (иначе парсер выдаёт
 * некорректную навигацию). Остальное контролируется санитайзерами.
 */
void check_invariants(const NMEAData &d) {
  if (d.has_lat_lon) {
    if (!d.lat.is_valid()) fail("latitude out of valid range");
    if (!d.lon.is_valid()) fail("longitude out of valid range");
  }
  // Скорость и курс не могут быть отрицательными (поле без знака по NMEA).
  if (d.speed_kn_x1000.raw < 0) fail("negative speed_kn");
  if (d.course_true.raw < 0) fail("negative course_true");
  if (d.course_mag.raw < 0) fail("negative course_mag");
  if (d.time_utc > 235959u) fail("time_utc out of range");
}

// ---------------------------------------------------------------------------
// Проверка на восстановление: после мусорного потока парсер должен(!) восстановиться
// ---------------------------------------------------------------------------

bool recovery_test() {
  Parser p;
  const std::string_view garbage[] = {
      "$GARBAGE,*00\r\n",
      "$$$***\r\n",
      "\x00\x01\x02\xff\xfe",
      "$GPRMC,",
      "*",
      "A\r\n",
  };
  for (const auto g : garbage) {
    (void)p.parse_line(g); // результат из мусорных данных не важен - важна устойчивость
  }
  // После мусорных данных - правильный пакет RMC из nav_gen (Мюнхен).
  const bool ok = p.parse_line(nav_gen::NAV_PACKETS[0]);
  const NMEAData &d = p.data();
  return ok && d.lat.is_valid() && d.lon.is_valid() && d.lat.raw != 0 &&
         d.lon.raw != 0;
}

// ---------------------------------------------------------------------------
// Мутации - изменения входных данных определенным образом
// ---------------------------------------------------------------------------

/**
 * Применяет одну мутацию к строке s (результат в out).
 * Возвращает false, если мутация не применилась (выход за границы) -
 * такие варианты пропускаются как невозможные.
 */
bool mutate(std::string s, uint64_t &rng, std::string &out) {
  const size_t len = s.size();
  if (len == 0) {
    out = s; // пустая строка
    return true;
  }
  switch (next_rand(rng) % 10u) {
    case 0: // без изменений (контрольный прогон)
      out = s;
      return true;
    case 1: { // замена одного байта
      const size_t i = static_cast<size_t>(next_rand(rng)) % len;
      s[i] ^= static_cast<char>(1u << (rnd_byte(rng) & 7u));
      out = s;
      return true;
    }
    case 2: { // обрыв на случайной позиции
      const size_t i = static_cast<size_t>(next_rand(rng)) % (len + 1);
      out = s.substr(0, i);
      return true;
    }
    case 3: { // удаление одного байта
      const size_t i = static_cast<size_t>(next_rand(rng)) % len;
      s.erase(i, 1);
      out = s;
      return true;
    }
    case 4: { // вставка случайного байта
      const size_t i = static_cast<size_t>(next_rand(rng)) % (len + 1);
      s.insert(i, 1, static_cast<char>(rnd_byte(rng)));
      out = s;
      return true;
    }
    case 5: { // мусорный префикс (до '$')
      s.insert(0, 1, static_cast<char>(rnd_byte(rng)));
      out = s;
      return true;
    }
    case 6: { // мусорный хвост после окончания
      const size_t n = 1 + (static_cast<size_t>(next_rand(rng)) % 16);
      for (size_t k = 0; k < n; ++k) s.push_back(static_cast<char>(rnd_byte(rng)));
      out = s;
      return true;
    }
    case 7: { // замена первого символа (ломает '$')
      s[0] = static_cast<char>(rnd_byte(rng));
      out = s;
      return true;
    }
    case 8: { // 'битая' контрольная сумма: 'переворот' hex-цифры после '*'
      const size_t star = s.find('*');
      if (star == std::string::npos || star + 2 >= len) {
        out = s;
        return true;
      }
      s[star + 1] = (s[star + 1] == '0') ? '1' : '0';
      out = s;
      return true;
    }
    default: { // полностью случайная строка до 128 байт
      const size_t n = static_cast<size_t>(next_rand(rng)) % 128;
      s.resize(n);
      for (size_t k = 0; k < n; ++k) s[k] = static_cast<char>(rnd_byte(rng));
      out = s;
      return true;
    }
  }
}

// ---------------------------------------------------------------------------
// Фазы теста
// ---------------------------------------------------------------------------

/**
 * Детерминированная фаза: по всем эталонным пакетам x все позиции
 * ('переворот'-флип, обрыв, удаление, вставка), плюс граничные случаи.
 */
size_t run_deterministic(Parser &parser) {
  size_t accepted = 0;
  const size_t tested = [] {
    size_t n = 0;
    for (const auto p : nav_gen::NAV_PACKETS) n += p.size();
    return n;
  }();
  std::printf("  deterministic phase over %zu packet-bytes...\n", tested);

  for (const auto base : nav_gen::NAV_PACKETS) {
    const std::string src(base);
    const size_t len = src.size();

    // Проверка 'как есть'
    if (parser.parse_line(src)) {
      check_invariants(parser.data());
      ++accepted;
    }
    // Обрыв на каждой позиции 0..len
    for (size_t i = 0; i <= len; ++i) {
      if (parser.parse_line(std::string_view(src).substr(0, i))) {
        check_invariants(parser.data());
        ++accepted;
      }
    }
    // 'Переворот' каждого байта
    for (size_t i = 0; i < len; ++i) {
      std::string m = src;
      m[i] ^= static_cast<char>(0xFF);
      if (parser.parse_line(m)) {
        check_invariants(parser.data());
        ++accepted;
      }
    }
    // Удаление каждого байта
    for (size_t i = 0; i < len; ++i) {
      std::string m = src;
      m.erase(i, 1);
      if (parser.parse_line(m)) {
        check_invariants(parser.data());
        ++accepted;
      }
    }
    // Вставка 'A' в каждую позицию
    for (size_t i = 0; i <= len; ++i) {
      std::string m = src;
      m.insert(i, 1, 'A');
      if (parser.parse_line(m)) {
        check_invariants(parser.data());
        ++accepted;
      }
    }
  }

  // Граничные случаи
  const std::string_view edges[] = {
      "", "$", "$\r\n", "$GP", "$GPRMC,\r\n", "*", "**\r\n",
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n",
  };
  for (const auto e : edges) {
    if (parser.parse_line(e)) {
      check_invariants(parser.data());
      ++accepted;
    }
  }

  // Переполнение длины: эталонный пакет + 200 байт мусора
  std::string long_src(nav_gen::NAV_PACKETS[0]);
  long_src.append(200, 'X');
  if (parser.parse_line(long_src)) {
    check_invariants(parser.data());
    ++accepted;
  }

  return accepted;
}

/**
 * Случайная фаза: N мутаций с помощью mutate().
 */
size_t run_random(Parser &parser, size_t n, uint64_t seed) {
  uint64_t rng = seed;
  size_t accepted = 0;
  std::string out;
  const size_t nbase = nav_gen::NAV_PACKETS.size();
  for (size_t it = 0; it < n; ++it) {
    const std::string base(nav_gen::NAV_PACKETS[next_rand(rng) % nbase]);
    if (!mutate(base, rng, out)) continue;
    if (parser.parse_line(out)) {
      check_invariants(parser.data());
      ++accepted;
    }
  }
  return accepted;
}

} // namespace

// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  size_t random_iters = 1'000'000;
  if (argc > 1) {
    const long v = std::strtol(argv[1], nullptr, 10);
    if (v > 0) random_iters = static_cast<size_t>(v);
  }

  std::printf("Running LightNMEA fuzz tests...\n");
  std::printf("  random iterations: %zu\n", random_iters);

  Parser parser;
  size_t total = 0;

  total += run_deterministic(parser);
  const size_t random_accepted = run_random(parser, random_iters, 0xDEADBEEFULL);
  total += random_accepted;
  std::printf("  random phase accepted: %zu\n", random_accepted);

  if (!recovery_test()) {
    fail("parser did not recover after garbage stream");
  }

  if (g_fail) {
    std::printf("\nFUZZ RESULT: FAIL (invariants violated)\n");
    return 1;
  }
  std::printf("\nFUZZ RESULT: OK (%zu mutations, invariants hold, memory clean)\n",
              total);
  return 0;
}