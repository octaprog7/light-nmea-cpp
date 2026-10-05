#include "light_nmea.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

using namespace light_nmea;

bool approx_equal(double a, double b, double epsilon = 1e-4) {
  return std::abs(a - b) < epsilon;
}

// NMEA-0183 контрольная сумма: XOR байт между '$' и '*'
uint8_t nmea_crc(std::string_view line) noexcept {
  uint8_t crc = 0;
  for (size_t i = 1; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '*') break;
    crc ^= static_cast<uint8_t>(c);
  }
  return crc;
}

int hex_val(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

void test_basic_rmc() {
  Parser parser;
  std::string_view nmea =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
  (void)nmea; // Подавляю предупреждение

  assert(parser.parse_line(nmea));
  const auto &data = parser.data();
  (void)data; // Подавляю предупреждение

  assert(data.valid == true);
  assert(data.constellation == Constellation::GPS);
  assert(data.time_utc == 123519);
  assert(data.date == 230394);
  assert(approx_equal(data.latitude_deg(), 48.1173));
  assert(approx_equal(data.longitude_deg(), 11.5167));
  assert(approx_equal(data.speed_knots(), 22.4));
  assert(approx_equal(data.course_true_deg(), 84.4));

  std::cout << "  test_basic_rmc passed\n";
}

void test_basic_gga() {
  Parser parser;
  std::string_view nmea =
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*4F";

  bool result = parser.parse_line(nmea);

  if (!result) {
    std::cout << "GGA parse failed!\n";
    std::cout << "Diagnostics:\n";
    std::cout << "  reject_crc: " << parser.diagnostics().reject_crc << "\n";
    std::cout << "  reject_unknown_cst: "
              << parser.diagnostics().reject_unknown_cst << "\n";
    std::cout << "  reject_filtered_cst: "
              << parser.diagnostics().reject_filtered_cst << "\n";
    std::cout << "  reject_too_short: " << parser.diagnostics().reject_too_short
              << "\n";
    std::cout << "  reject_unknown_msg: "
              << parser.diagnostics().reject_unknown_msg << "\n";
  }

  assert(result);
  const auto &data = parser.data();
  (void)data;

  assert(data.constellation == Constellation::GPS);
  assert(data.satellites == 8);
  assert(approx_equal(data.hdop_value(), 0.9));
  assert(approx_equal(data.altitude_m(), 545.4));

  std::cout << " test_basic_gga passed\n";
}

void test_invalid_crc() {
  Parser::Config cfg;
  cfg.enable_diagnostics = true;
  Parser parser(cfg);
  std::string_view nmea =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*FF";
  (void)nmea;
  assert(!parser.parse_line(nmea));
  assert(parser.diagnostics().reject_crc == 1);
  std::cout << " test_invalid_crc passed\n";
}

void test_constellation_filter() {
  Parser::Config cfg;
  cfg.enable_diagnostics = true;
  Parser parser(cfg);
  parser.set_cst_filter(CST_MASK_GPS);
  std::string_view nmea_glonass =
      "$GLRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*76";
  (void)nmea_glonass;
  assert(!parser.parse_line(nmea_glonass));
  assert(parser.diagnostics().reject_filtered_cst == 1);
  std::string_view nmea_gps =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
  (void)nmea_gps;
  assert(parser.parse_line(nmea_gps));
  std::cout << " test_constellation_filter passed\n";
}

void test_negative_coordinates() {
  Parser parser;
  std::string_view nmea =
      "$GPRMC,123519,A,3349.000,S,11807.000,W,022.4,084.4,230394,003.1,W*65";
  (void)nmea;

  assert(parser.parse_line(nmea));
  const auto &data = parser.data();
  (void)data;

  assert(data.latitude_deg() < 0);
  assert(data.longitude_deg() < 0);

  std::cout << " test_negative_coordinates passed\n";
}

void test_course_precision() {
  // Регрессия parse_value_e1(): при ровно одном знаке после запятой
  // курс не должен терять дробную часть. "084.4" -> 84.40 (масштаб e2).
  Parser parser;
  std::string_view nmea =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

  assert(parser.parse_line(nmea));
  const auto &data = parser.data();

  // course_true.raw хранится в e2 (сотые градуса): 84.40 -> 8440
  assert(data.course_true.raw == 8440);
  assert(approx_equal(data.course_true_deg(), 84.4));

  std::cout << "  test_course_precision passed\n";
}

void test_coordinates_variants() {
  // Проверка parse_degrees_e7 (оптимизация без find) на разных форматах
  // координат и направлениях: DDMM.MMM / DDDMM.MMMM / дробь разной длины / N/S/E/W.
  Parser parser;

  // Северо-восточные, DDMM / DDDMM
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
  assert(approx_equal(parser.data().latitude_deg(), 48.1173));
  assert(approx_equal(parser.data().longitude_deg(), 11.5167));

  // Дробная часть минут с 4 знаками
  assert(parser.parse_line(
      "$GPGGA,092750.000,5545.1234,N,03731.0000,E,1,08,1.1,150.0,M,15.0,M,,*5E"));
  assert(approx_equal(parser.data().latitude_deg(), 55.7521));
  assert(approx_equal(parser.data().longitude_deg(), 37.516667, 1e-5));

  // Южное/Западное полушарие -> отрицательные координаты
  assert(parser.parse_line(
      "$GPRMC,123519,A,3349.000,S,11807.000,W,022.4,084.4,230394,003.1,W*65"));
  assert(approx_equal(parser.data().latitude_deg(), -33.8167));
  assert(approx_equal(parser.data().longitude_deg(), -118.1167));

  std::cout << "  test_coordinates_variants passed\n";
}

void test_packet_crcs() {
  // Те же пакеты, что в benchmark/main.cpp (TEST_PACKETS).
  // Проверяем, что заявленная контрольная сумма совпадает с вычисленной,
  // иначе бенчмарк молча отбраковал бы все пакеты.
  constexpr std::array<std::string_view, 6> packets = {{
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
      "$GPGGA,092750.000,5545.1234,N,03731.0000,E,1,08,1.1,150.0,M,15.0,M,,*5E",
      "$GPRMC,092750.000,A,5545.1234,N,03731.0000,E,0.02,31.66,280511,,,A*5B",
      "$GNGGA,123519,4807.038,N,01131.000,E,1,12,0.8,545.4,M,46.9,M,,*53",
      "$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*74",
  }};

  for (const auto &p : packets) {
    const size_t star = p.find('*');
    assert(star != std::string_view::npos && star + 2 < p.size());
    const int n1 = hex_val(p[star + 1]);
    const int n2 = hex_val(p[star + 2]);
    assert(n1 >= 0 && n2 >= 0);
    const uint8_t expected = static_cast<uint8_t>((n1 << 4) | n2);
    assert(nmea_crc(p) == expected);
  }
  std::cout << "  test_packet_crcs passed\n";
}

int main() {
  std::cout << "Running LightNMEA tests...\n\n";

  test_basic_rmc();
  test_basic_gga();
  test_invalid_crc();
  test_constellation_filter();
  test_negative_coordinates();
  test_course_precision();
  test_coordinates_variants();
  test_packet_crcs();

  std::cout << "\n All tests passed!\n";
  return 0;
}