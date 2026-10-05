#include "light_nmea.hpp"
#include "nmea_formatter.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace light_nmea;

// Helper для сравнения строк
bool approx_equal(double a, double b, double epsilon = 1e-4) {
  return std::abs(a - b) < epsilon;
}

// ============================================================================
// Тест 1: TXT формат
// ============================================================================
void test_txt_format() {
  Parser parser;
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  const auto &data = parser.data();
  char buffer[512];

  size_t len = to_txt(data, buffer, sizeof(buffer));
  (void)len;
  std::string output(buffer);

  // Проверяем наличие ключевых строк
  assert(output.find("Fix: Yes") != std::string::npos);
  assert(output.find("satellites: 0") != std::string::npos);
  assert(output.find("latitude: 48.117300") != std::string::npos);
  assert(output.find("longitude: 11.516667") != std::string::npos);
  assert(output.find("speed: 41.5 km/h") != std::string::npos);
  assert(output.find("course: 84.4") != std::string::npos);
  assert(output.find("time: 12:35:19 UTC") != std::string::npos);
  assert(output.find("date: 23.03.94") != std::string::npos);

  // Проверяем длину
  assert(len > 0);
  assert(len < sizeof(buffer));

  std::cout << "OK test_txt_format passed\n";
}

// ============================================================================
// Тест 2: CSV формат
// ============================================================================
void test_csv_format() {
  Parser parser;
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  const auto &data = parser.data();
  char buffer[512];

  size_t len = to_csv(data, buffer, sizeof(buffer));
  (void)len;
  std::string output(buffer);

  // CSV должен содержать запятые
  assert(output.find(',') != std::string::npos);

  // Проверяем поля
  assert(output.find("1,") == 0);                        // valid = 1
  assert(output.find(",0,") != std::string::npos);       // satellites = 0
  assert(output.find("48.117300") != std::string::npos); // latitude
  assert(output.find("11.516667") != std::string::npos); // longitude
  assert(output.find("41.5") != std::string::npos);      // speed
  assert(output.find("84.4") != std::string::npos);      // course
  assert(output.find("12:35:19") != std::string::npos);  // time
  assert(output.find("23.03.94") != std::string::npos);  // date
  assert(output.find("GPS") != std::string::npos);       // constellation

  assert(len > 0);
  assert(len < sizeof(buffer));

  std::cout << "OK test_csv_format passed\n";
}

// ============================================================================
// Тест 3: JSON формат
// ============================================================================
void test_json_format() {
  Parser parser;
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  const auto &data = parser.data();
  char buffer[512];

  size_t len = to_json(data, buffer, sizeof(buffer));
  (void)len;
  std::string output(buffer);

  // JSON должен начинаться с { и заканчиваться }
  assert(output[0] == '{');
  assert(output[len - 1] == '}');

  // Проверяем ключи и значения
  assert(output.find("\"valid\":true") != std::string::npos);
  assert(output.find("\"satellites\":0") != std::string::npos);
  assert(output.find("\"latitude\":48.117300") != std::string::npos);
  assert(output.find("\"longitude\":11.516667") != std::string::npos);
  assert(output.find("\"speed\":41.5") != std::string::npos);
  assert(output.find("\"course\":84.4") != std::string::npos);
  assert(output.find("\"time\":\"12:35:19\"") != std::string::npos);
  assert(output.find("\"date\":\"23.03.94\"") != std::string::npos);

  assert(len > 0);
  assert(len < sizeof(buffer));

  std::cout << "OK test_json_format passed\n";
}

// ============================================================================
// Тест 4: Compact формат
// ============================================================================
void test_compact_format() {
  Parser parser;
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  const auto &data = parser.data();
  char buffer[512];

  size_t len = to_compact(data, buffer, sizeof(buffer));
  (void)len;
  std::string output(buffer);

  // Compact должен быть однострочным (без \n)
  assert(output.find('\n') == std::string::npos);

  // Проверяем содержимое
  assert(output.find("FIX") == 0);
  assert(output.find("satellites=0") != std::string::npos);
  assert(output.find("48.117300,11.516667") != std::string::npos);
  assert(output.find("speed=41.5km/h") != std::string::npos);
  assert(output.find("course=84.4") != std::string::npos);
  assert(output.find("time=12:35:19") != std::string::npos);
  assert(output.find("date=23.03.94") != std::string::npos);

  assert(len > 0);
  assert(len < sizeof(buffer));

  std::cout << "OK test_compact_format passed\n";
}

// ============================================================================
// Тест 5: Защита от переполнения буфера
// ============================================================================
void test_buffer_overflow() {
  Parser parser;
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  const auto &data = parser.data();

  // Очень маленький буфер
  char small_buffer[10];
  size_t len = to_txt(data, small_buffer, sizeof(small_buffer));
  (void)len;
  // Должен обрезаться, но не выйти за границы
  assert(len <= sizeof(small_buffer) - 1);
  assert(small_buffer[sizeof(small_buffer) - 1] == '\0');

  // Буфер нулевого размера
  char zero_buffer[1];
  len = to_txt(data, zero_buffer, sizeof(zero_buffer));
  assert(len == 0);
  assert(zero_buffer[0] == '\0');

  std::cout << "OK test_buffer_overflow passed\n";
}

// ============================================================================
// Тест 6: Пустые данные
// ============================================================================
void test_empty_data() {
  NMEAData empty_data;
  char buffer[512];

  // TXT с пустыми данными
  size_t len = to_txt(empty_data, buffer, sizeof(buffer));
  std::string output(buffer);
  (void)len;
  assert(output.find("Fix: No") != std::string::npos);
  assert(output.find("NO_FIX") != std::string::npos ||
         output.find("Fix: No") != std::string::npos);

  // JSON с пустыми данными
  len = to_json(empty_data, buffer, sizeof(buffer));
  output = std::string(buffer);

  assert(output.find("\"valid\":false") != std::string::npos);
  assert(output.find("\"satellites\":0") != std::string::npos);

  // Compact с пустыми данными
  len = to_compact(empty_data, buffer, sizeof(buffer));
  output = std::string(buffer);

  assert(output.find("NO_FIX") != std::string::npos);

  std::cout << "OK test_empty_data passed\n";
}

// ============================================================================
// Тест 7: Разные единицы скорости
// ============================================================================
void test_speed_units() {
  Parser parser;
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  const auto &data = parser.data();
  char buffer[512];

  // Км/ч
  to_txt(data, buffer, sizeof(buffer), SpeedUnit::KMH);
  std::string output(buffer);
  assert(output.find("km/h") != std::string::npos);

  // Узлы
  to_txt(data, buffer, sizeof(buffer), SpeedUnit::KNOTS);
  output = std::string(buffer);
  assert(output.find("knots") != std::string::npos);
  assert(output.find("22.4") != std::string::npos); // 22.4 узла

  // М/с
  to_txt(data, buffer, sizeof(buffer), SpeedUnit::MS);
  output = std::string(buffer);
  assert(output.find("m/s") != std::string::npos);

  std::cout << "OK test_speed_units passed\n";
}

// ============================================================================
// Тест 8: Разные единицы высоты
// ============================================================================
void test_distance_units() {
  Parser parser;
  assert(parser.parse_line(
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*4F"));

  const auto &data = parser.data();
  char buffer[512];

  // Метры
  to_txt(data, buffer, sizeof(buffer), SpeedUnit::KMH, DistanceUnit::METERS);
  std::string output(buffer);
  assert(output.find("545.4 m") != std::string::npos);

  // Футы
  to_txt(data, buffer, sizeof(buffer), SpeedUnit::KMH, DistanceUnit::FEET);
  output = std::string(buffer);
  assert(output.find("ft") != std::string::npos);
  // 545.4 м = 1789.4 фута
  assert(output.find("1789.4") != std::string::npos);

  // Километры
  to_txt(data, buffer, sizeof(buffer), SpeedUnit::KMH,
         DistanceUnit::KILOMETERS);
  output = std::string(buffer);
  assert(output.find("km") != std::string::npos);
  assert(output.find("0.5") != std::string::npos); // 0.5454 км ~ 0.5

  std::cout << "OK test_distance_units passed\n";
}

// ============================================================================
// Тест 9: Универсальная функция to_format
// ============================================================================
void test_to_format() {
  Parser parser;
  assert(parser.parse_line(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));

  const auto &data = parser.data();
  char buffer[512];

  // TXT
  size_t len = to_format(data, buffer, sizeof(buffer), Format::TXT);
  assert(len > 0);
  assert(std::string(buffer).find("Fix: Yes") != std::string::npos);

  // CSV
  len = to_format(data, buffer, sizeof(buffer), Format::CSV);
  assert(len > 0);
  assert(std::string(buffer).find(',') != std::string::npos);

  // JSON
  len = to_format(data, buffer, sizeof(buffer), Format::JSON);
  assert(len > 0);
  assert(std::string(buffer)[0] == '{');

  // Compact
  len = to_format(data, buffer, sizeof(buffer), Format::COMPACT);
  assert(len > 0);
  assert(std::string(buffer).find('\n') == std::string::npos);

  std::cout << "OK test_to_format passed\n";
}

// ============================================================================
// Тест 10: GGA данные (с высотой и спутниками)
// ============================================================================
void test_gga_format() {
  Parser parser;
  assert(parser.parse_line(
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*4F"));

  const auto &data = parser.data();
  char buffer[512];

  to_txt(data, buffer, sizeof(buffer));
  std::string output(buffer);

  assert(output.find("satellites: 8") != std::string::npos);
  assert(output.find("altitude: 545.4 m") != std::string::npos);

  to_json(data, buffer, sizeof(buffer));
  output = std::string(buffer);

  assert(output.find("\"satellites\":8") != std::string::npos);
  assert(output.find("\"altitude\":545.4") != std::string::npos);

  std::cout << "OK test_gga_format passed\n";
}

// ============================================================================
// Main
// ============================================================================
int main() {
  std::cout << "Running NMEA Formatter Tests...\n\n";

  test_txt_format();
  test_csv_format();
  test_json_format();
  test_compact_format();
  test_buffer_overflow();
  test_empty_data();
  test_speed_units();
  test_distance_units();
  test_to_format();
  test_gga_format();

  std::cout << "\nAll formatter tests passed!\n";

  return 0;
}