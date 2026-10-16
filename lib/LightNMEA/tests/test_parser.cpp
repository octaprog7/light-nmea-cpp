#include "light_nmea.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

using namespace light_nmea;

bool approx_equal(double a, double b, double epsilon = 1e-4) {
  return std::abs(a - b) < epsilon;
}

// Возвращает контрольную сумму NMEA-0183: XOR байт между '$' и '*'
uint8_t nmea_crc(std::string_view line) noexcept {
  uint8_t crc = 0;
  for (size_t i = 1; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '*') break;
    crc ^= static_cast<uint8_t>(c);
  }
  return crc;
}

// Формирует полную NMEA-строку, дописывая верную контрольную сумму.
std::string with_crc(std::string_view body) {
  char buf[160];
  const int len = std::snprintf(buf, sizeof(buf), "%s*%02X",
                                std::string(body).c_str(),
                                static_cast<unsigned>(nmea_crc(body)));
  return std::string(buf, static_cast<size_t>(len));
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
  (void)nmea; // подавляю предупреждение

  assert(parser.parse_line(nmea));
  const auto &data = parser.data();
  (void)data; // подавляю предупреждение

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
  // B1: время из поля 1 GGA (поток без RMC/GLL).
  assert(data.time_utc == 123519);

  std::cout << " test_basic_gga passed\n";
}

void test_negative_altitude() {
  // NMEA-0183, Таблица 6, примечание 2: "-" (HEX 2D) - первый символ
  // числового поля, если значение отрицательное. Высота над уровнем моря в GGA
  // бывает отрицательной. Раньше parse_value_e2() терял знак, и "-545.4" превращалось в 545.4 м (потеря знака).
  Parser parser;
  std::string_view nmea =
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,-545.4,M,47.0,M,,*62";
  (void)nmea;

  assert(parser.parse_line(nmea));
  const auto &data = parser.data();
  (void)data;

  assert(data.valid == true);
  assert(data.altitude_cm == -54540); // масштаб x100: -545.4 м
  assert(approx_equal(data.altitude_m(), -545.4));

  std::cout << "  test_negative_altitude passed\n";
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

void test_rmc_mode_indicator() {
  // FAA Mode Indicator (NMEA 2.3+, по документации gpsd):
  // F = RTK Float, R = RTK Integer (fixed integers).
  Parser parser;

  const auto line_f = with_crc(
      "$GNRMC,001031.00,A,4404.13993,N,12118.86023,W,0.146,,100117,,,F");
  assert(parser.parse_line(line_f));
  assert(parser.data().valid == true);
  assert(parser.data().fix_mode == FixMode::RTKFloat);

  const auto line_r = with_crc(
      "$GNRMC,001031.00,A,4404.13993,N,12118.86023,W,0.146,,100117,,,R");
  assert(parser.parse_line(line_r));
  assert(parser.data().fix_mode == FixMode::RTKFixed);

  // P = Precise (NMEA 4.00+).
  const auto line_p = with_crc(
      "$GNRMC,001031.00,A,4404.13993,N,12118.86023,W,0.146,,100117,,,P");
  assert(parser.parse_line(line_p));
  assert(parser.data().fix_mode == FixMode::Precise);

  std::cout << "  test_rmc_mode_indicator passed\n";
}

void test_gga_quality_6_estimated() {
  // GGA GPS Quality 6 = estimated (dead reckoning): при trust_gga_fix = true
  // должен давать FixMode::Estimated (gpsd / NMEA 2.3+).
  Parser::Config cfg;
  cfg.trust_gga_fix = true;
  Parser parser(cfg);

  const auto line = with_crc(
      "$GPGGA,123519,4807.038,N,01131.000,E,6,08,0.9,545.4,M,47.0,M,,");
  assert(parser.parse_line(line));
  assert(parser.data().valid == true);
  assert(parser.data().fix_mode == FixMode::Estimated);

  // Quality 8 (simulation), 7 (manual input), 3 (PPS) - валидные фиксы
  // с корректной меткой режима (C1).
  const auto line_sim = with_crc(
      "$GPGGA,123519,4807.038,N,01131.000,E,8,08,0.9,545.4,M,47.0,M,,");
  assert(parser.parse_line(line_sim));
  assert(parser.data().valid == true);
  assert(parser.data().fix_mode == FixMode::Simulator);

  const auto line_man = with_crc(
      "$GPGGA,123519,4807.038,N,01131.000,E,7,08,0.9,545.4,M,47.0,M,,");
  assert(parser.parse_line(line_man));
  assert(parser.data().valid == true);
  assert(parser.data().fix_mode == FixMode::ManualInput);

  const auto line_pps = with_crc(
      "$GPGGA,123519,4807.038,N,01131.000,E,3,08,0.9,545.4,M,47.0,M,,");
  assert(parser.parse_line(line_pps));
  assert(parser.data().valid == true);
  assert(parser.data().fix_mode == FixMode::PPS);

  std::cout << "  test_gga_quality_6_estimated passed\n";
}

void test_speed_precision_e3() {
  // Поле B4: скорость хранится в тысячных долях узла - "0.146" (пример gpsd) не теряет
  // точность, как раньше с сотыми (0.146 -> 0.14).
  Parser parser;
  const auto line = with_crc(
      "$GNRMC,001031.00,A,4404.13993,N,12118.86023,W,0.146,,100117,,,A");
  assert(parser.parse_line(line));
  assert(parser.data().speed_kn_x1000.raw == 146);
  assert(approx_equal(parser.data().speed_knots(), 0.146, 1e-6));

  std::cout << "  test_speed_precision_e3 passed\n";
}

void test_coordinates_no_decimal_point() {
  // A2: десятичная точка опциональна (Таблица 6). "4807" = 48 deg 07',
  // "01131" = 11 deg 31'.
  Parser parser;
  const auto line = with_crc(
      "$GPRMC,123519,A,4807,N,01131,E,022.4,084.4,230394,003.1,W");
  assert(parser.parse_line(line));
  assert(approx_equal(parser.data().latitude_deg(), 48.0 + 7.0 / 60.0, 1e-5));
  assert(approx_equal(parser.data().longitude_deg(), 11.0 + 31.0 / 60.0, 1e-5));

  // Менее 4 целых цифр без точки - невалидно (ddmm минимум).
  const auto short_field = with_crc(
      "$GPRMC,123519,A,807,N,01131,E,022.4,084.4,230394,003.1,W");
  assert(!parser.parse_line(short_field));

  std::cout << "  test_coordinates_no_decimal_point passed\n";
}

void test_invalid_direction_rejected() {
  // A3: направление должно быть N/S (широта) и E/W (долгота).
  // Мусорная буква или пустое поле (null = нет данных) -> reject.
  Parser parser;

  const auto bad_dir = with_crc(
      "$GPRMC,123519,A,4807.038,X,01131.000,E,022.4,084.4,230394,003.1,W");
  assert(!parser.parse_line(bad_dir));

  const auto empty_dir = with_crc(
      "$GPRMC,123519,A,4807.038,,01131.000,E,022.4,084.4,230394,003.1,W");
  assert(!parser.parse_line(empty_dir));

  // Южное/Западное полушарие по-прежнему работает.
  const auto good = with_crc(
      "$GPRMC,123519,A,4807.038,S,01131.000,W,022.4,084.4,230394,003.1,W");
  assert(parser.parse_line(good));
  assert(approx_equal(parser.data().latitude_deg(), -48.1173));
  assert(approx_equal(parser.data().longitude_deg(), -11.5167));

  std::cout << "  test_invalid_direction_rejected passed\n";
}

void test_address_field_length() {
  // A4: адресное поле ровно 5 символов (5.2.1.1); "GPRMCFOO" -> reject.
  Parser parser;

  const auto long_addr = with_crc(
      "$GPRMCFOO,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
  assert(!parser.parse_line(long_addr));

  const auto good = with_crc(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
  assert(parser.parse_line(good));

  std::cout << "  test_address_field_length passed\n";
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
  [[maybe_unused]] std::string_view nmea =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

  assert(parser.parse_line(nmea));
  [[maybe_unused]] const auto &data = parser.data();

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
  // Проверяет совпадение контрольной суммы из пакета/сентенции с вычисленным по принятым данным пакета значением,
  // иначе бенчмарк молча отбраковал бы все пакеты/сентенции.
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
    [[maybe_unused]] const uint8_t expected = static_cast<uint8_t>((n1 << 4) | n2);
    assert(nmea_crc(p) == expected);
  }
  std::cout << "  test_packet_crcs passed\n";
}

void test_fields_without_decimal_point() {
  // Некоторые GNSS-приёмники передают числовые поля БЕЗ десятичной точки
  // (Таблица 6: точка и дробь опциональны). Парсер должен трактовать их как
  // целые значения в фиксированном масштабе поля:
  //   скорость "022" -> 22.000 узла, курс "084" -> 84.00 deg,
  //   высота "545" -> 545.00 м, HDOP "1" -> 1.00.
  Parser parser;

  // RMC без десятичных точек: координаты, скорость и курс.
  const auto rmc = with_crc(
      "$GPRMC,123519,A,4807,N,01131,E,022,084,230394,003.1,W");
  assert(parser.parse_line(rmc));
  assert(parser.data().valid == true);
  assert(parser.data().speed_kn_x1000.raw == 22000); // 22.000 узла
  assert(approx_equal(parser.data().speed_knots(), 22.0));
  assert(parser.data().course_true.raw == 8400);     // 84.00 градуса
  assert(approx_equal(parser.data().course_true_deg(), 84.0));
  // Координаты без точки: "4807"/"01131" -> ddmm (уже покрыто отдельным тестом).
  assert(approx_equal(parser.data().latitude_deg(), 48.0 + 7.0 / 60.0, 1e-5));
  assert(approx_equal(parser.data().longitude_deg(), 11.0 + 31.0 / 60.0, 1e-5));

  // GGA без десятичных точек: высота и HDOP.
  const auto gga = with_crc(
      "$GPGGA,123519,4807,N,01131,E,1,08,1,545,M,47.0,M,,");
  assert(parser.parse_line(gga));
  assert(parser.data().altitude_cm == 54500);        // 545.00 м
  assert(approx_equal(parser.data().altitude_m(), 545.0));
  assert(parser.data().hdop_e2 == 100);              // 1.00
  assert(approx_equal(parser.data().hdop_value(), 1.0));

  // VTG без десятичных точек: курс и обе скорости (узлы и км/ч).
  const auto vtg = with_crc(
      "$GPVTG,084,T,085,M,022,N,041,K");
  assert(parser.parse_line(vtg));
  assert(parser.data().course_true.raw == 8400);
  assert(parser.data().course_mag.raw == 8500);
  assert(parser.data().speed_kn_x1000.raw == 22000);
  assert(parser.data().speed_kmh_x1000.raw == 41000);

  std::cout << "  test_fields_without_decimal_point passed\n";
}

int main() {
  std::cout << "Running LightNMEA tests...\n\n";

  test_basic_rmc();
  test_basic_gga();
  test_negative_altitude();
  test_rmc_mode_indicator();
  test_gga_quality_6_estimated();
  test_speed_precision_e3();
  test_fields_without_decimal_point();
  test_coordinates_no_decimal_point();
  test_invalid_direction_rejected();
  test_address_field_length();
  test_invalid_crc();
  test_constellation_filter();
  test_negative_coordinates();
  test_course_precision();
  test_coordinates_variants();
  test_packet_crcs();

  std::cout << "\n All tests passed!\n";
  return 0;
}