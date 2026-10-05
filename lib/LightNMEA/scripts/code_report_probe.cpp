// ============================================================================
// code_report_probe.cpp - репрезентативный сценарий использования LightNMEA
// для отчёта code_report.sh.
//
// Намеренно задействуется ТОЛЬКО целочисленное ядро библиотеки:
//   * Parser::parse_line()  - разбор NMEA-строки (CRC, поля, масштабирование)
//   * доступ к "сырым" значениям (.raw) - без float-конвертеров
//
// Float-геттеры NMEAData (latitude_deg(), speed_knots(), altitude_m(), ...)
// и nmea_formatter.hpp НЕ включаются: они добавляют float-арифметику
// (на Cortex-M0+ - медленный soft-float) по явному требованию пользователя
// и не относятся к "integer core", который здесь измеряется.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <light_nmea.hpp>

// Внешнее связывание: при сборке объектного файла (-c) компилятор обязан
// сгенерировать и сохранить весь используемый код парсера.
extern "C" uint32_t lightnmea_probe(const char *line, size_t len);

extern "C" uint32_t lightnmea_probe(const char *line, size_t len) {
  light_nmea::Parser parser;

  const std::string_view sentence(line, len);
  if (!parser.parse_line(sentence)) {
    return 0u;
  }

  // Свёртка сырых целочисленных результатов в контрольную сумму, чтобы все
  // ветки парсера (RMC/GGA/VTG/GLL) оставались "живыми" для компилятора.
  const auto &d = parser.data();
  uint32_t acc = 0xBEEF0000u;
  if (d.has_coordinates()) {
    acc ^= static_cast<uint32_t>(d.lat.raw);
    acc ^= static_cast<uint32_t>(d.lon.raw) << 1;
  }
  if (d.has_navigation()) {
    acc ^= static_cast<uint32_t>(d.speed_kn_x1000.raw);
    acc ^= static_cast<uint32_t>(d.course_true.raw) << 2;
  }
  if (d.has_3d_fix()) {
    acc ^= static_cast<uint32_t>(d.altitude_cm) << 3;
    acc ^= static_cast<uint32_t>(d.hdop_e2) << 4;
    acc ^= static_cast<uint32_t>(d.satellites) << 5;
  }
  acc ^= d.time_utc << 6;
  acc ^= d.date << 7;
  if (d.valid) {
    acc ^= 0x1u;
  }
  return acc;
}