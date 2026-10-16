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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "types.hpp" // <-- Интеграция сильных типов

// Переносимая пометка принудительного инлайнинга для GCC/Clang/MSVC.
#if defined(_MSC_VER)
  #define LIGHT_NMEA_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
  #define LIGHT_NMEA_ALWAYS_INLINE inline __attribute__((always_inline))
#else
  #define LIGHT_NMEA_ALWAYS_INLINE inline
#endif

// Переносимые подсказки ветвления [[likely]]/[[unlikely]] (C++20).
// GCC >= 9 и Clang >= 12 принимают их и в режиме C++17 как расширение;
// для более старых компиляторов (например, avr-gcc 7.3 из Arduino AVR core)
// макрос раскрывается в пустоту, и код остаётся валидным C++17.
#if defined(__has_cpp_attribute)
  #if __has_cpp_attribute(likely)
    #define LIGHT_NMEA_LIKELY [[likely]]
    #define LIGHT_NMEA_UNLIKELY [[unlikely]]
  #else
    #define LIGHT_NMEA_LIKELY
    #define LIGHT_NMEA_UNLIKELY
  #endif
#else
  #define LIGHT_NMEA_LIKELY
  #define LIGHT_NMEA_UNLIKELY
#endif

namespace light_nmea {

// ============================================================================
// C++20 helper: to_underlying
// ============================================================================
template <typename E> constexpr auto to_underlying(E e) noexcept {
  static_assert(std::is_enum_v<E>, "to_underlying requires enum type");
  return static_cast<std::underlying_type_t<E>>(e);
}

// ============================================================================
// Константы и лимиты
// ============================================================================
inline constexpr size_t MAX_PACKET_SIZE = 120;
inline constexpr size_t MAX_COMMAS = 14;
inline constexpr size_t MIN_PACKET_LEN = 10;

inline constexpr char CHAR_DOLLAR = '$';
inline constexpr char CHAR_STAR = '*';
inline constexpr char CHAR_COMMA = ',';
inline constexpr char CHAR_0 = '0';
inline constexpr char CHAR_MINUS = '-';
inline constexpr char CHAR_A = 'A';
inline constexpr char CHAR_G = 'G';
inline constexpr char CHAR_R = 'R';
inline constexpr char CHAR_M = 'M';
inline constexpr char CHAR_C = 'C';
inline constexpr char CHAR_S = 'S';
inline constexpr char CHAR_W = 'W';
inline constexpr char CHAR_V = 'V';
inline constexpr char CHAR_T = 'T';
inline constexpr char CHAR_K = 'K';
inline constexpr char CHAR_N = 'N';
inline constexpr char CHAR_E = 'E';
inline constexpr char CHAR_L = 'L';

// ============================================================================
// Перечисления (строгая типизация)
// ============================================================================
enum class Constellation : uint8_t {
  Unknown = 0, GPS, GLONASS, Galileo, BeiDou, QZSS, NavIC, Multi
};

enum class FixMode : uint8_t {
  Autonomous = 0, DGPS, Estimated, NotValid, RTKFixed, RTKFloat,
  // Дополнительные режимы (в конце списка - обратная совместимость
  // числовых значений): GGA quality 3/7/8 и FAA Mode 'P' (NMEA 4.00+).
  PPS, ManualInput, Simulator, Precise
};

enum class MessageType : uint8_t { Unknown = 0, RMC, GGA, VTG, GLL };

inline constexpr uint32_t CST_MASK_GPS = (1u << to_underlying(Constellation::GPS));
inline constexpr uint32_t CST_MASK_GLONASS = (1u << to_underlying(Constellation::GLONASS));
inline constexpr uint32_t CST_MASK_GALILEO = (1u << to_underlying(Constellation::Galileo));
inline constexpr uint32_t CST_MASK_BEIDOU = (1u << to_underlying(Constellation::BeiDou));
inline constexpr uint32_t CST_MASK_QZSS = (1u << to_underlying(Constellation::QZSS));
inline constexpr uint32_t CST_MASK_NAVIC = (1u << to_underlying(Constellation::NavIC));
inline constexpr uint32_t CST_MASK_MULTI = (1u << to_underlying(Constellation::Multi));
inline constexpr uint32_t CST_MASK_ALL = 0xFFFFFFFF;

// ============================================================================
// Lookup-таблицы (constexpr для гарантии compile-time)
// ============================================================================
namespace detail {

constexpr auto make_cst_lookup() {
  std::array<Constellation, 17> table{};
  table['A' - 'A'] = Constellation::Galileo;
  table['B' - 'A'] = Constellation::BeiDou;
  table['I' - 'A'] = Constellation::NavIC;
  table['L' - 'A'] = Constellation::GLONASS;
  table['N' - 'A'] = Constellation::Multi;
  table['P' - 'A'] = Constellation::GPS;
  table['Q' - 'A'] = Constellation::QZSS;
  return table;
}

constexpr auto make_fix_mode_table() {
  // FAA Mode Indicator (NMEA 2.3+, по документации gpsd):
  // A/D/E/F/M/N/R/S определены; P = Precise (NMEA 4.00+).
  std::array<FixMode, 18> table{};
  for (auto &v : table) v = FixMode::NotValid;
  table['A' - 'A'] = FixMode::Autonomous;
  table['D' - 'A'] = FixMode::DGPS;
  table['E' - 'A'] = FixMode::Estimated;
  table['F' - 'A'] = FixMode::RTKFloat; // F = RTK Float
  table['P' - 'A'] = FixMode::Precise;  // P = Precise (4.00+)
  table['R' - 'A'] = FixMode::RTKFixed; // R = RTK Integer (fixed integers)
  return table;
}

constexpr auto make_gga_quality_table() {
  // GGA GPS Quality (gpsd / NMEA 2.3+): 0 invalid, 1 GPS, 2 DGPS, 3 PPS,
  // 4 RTK, 5 Float RTK, 6 estimated (dead reckoning), 7 manual input,
  // 8 simulation. 0 - единственный "нет фикса"; остальные - режимы с данными.
  std::array<FixMode, 9> table{};
  for (auto &v : table) v = FixMode::NotValid;
  table[1] = FixMode::Autonomous;   // GPS fix
  table[2] = FixMode::DGPS;         // Differential GPS
  table[3] = FixMode::PPS;          // GPS PPS fix
  table[4] = FixMode::RTKFixed;     // Real Time Kinematic
  table[5] = FixMode::RTKFloat;     // Float RTK
  table[6] = FixMode::Estimated;    // estimated (dead reckoning)
  table[7] = FixMode::ManualInput;  // manual input mode
  table[8] = FixMode::Simulator;    // simulation mode
  return table;
}

inline constexpr auto CST_LOOKUP_BY_SECOND_BYTE = make_cst_lookup();
inline constexpr auto FIX_MODE_TABLE = make_fix_mode_table();
inline constexpr auto GGA_QUALITY_FIX_MODE = make_gga_quality_table();

} // namespace detail

// ============================================================================
// Структура с распарсенными данными (TYPE-SAFE!)
// ============================================================================
struct NMEAData {
  // --- Координаты (Type-Safe) ---
  Latitude  lat;
  Longitude lon;

  // --- Скорость (Type-Safe, хранит int32_t внутри) ---
  Speed speed_kn_x1000;   // тысячные доли узла (из RMC и VTG, поле N)
  Speed speed_kmh_x1000;  // тысячные доли км/ч (из VTG, поле K)

  // --- Курс (Type-Safe, хранит int32_t внутри, масштаб 1e-2 градуса) ---
  Heading course_true;   // истинный курс
  Heading course_mag;    // магнитный курс

  // --- Высота и точность ---
  int32_t altitude_cm = 0; 
  int16_t hdop_e2 = 0;     

  uint8_t satellites = 0;
  bool valid = false;

  uint32_t time_utc = 0; 
  uint32_t date = 0;     

  Constellation constellation = Constellation::Unknown;
  FixMode fix_mode = FixMode::NotValid;
  MessageType msg_type = MessageType::Unknown;

  bool has_lat_lon = false;
  bool has_speed_course = false;
  bool has_altitude = false;
  bool has_vtg_speed = false;
  bool has_vtg_course = false;

  // === Helper-методы для конвертации (обновлены под .raw) ===

  [[nodiscard]] double latitude_deg() const noexcept {
    return static_cast<double>(lat.raw) / 10000000.0;
  }

  [[nodiscard]] double longitude_deg() const noexcept {
    return static_cast<double>(lon.raw) / 10000000.0;
  }

  [[nodiscard]] float speed_knots() const noexcept {
    return static_cast<float>(speed_kn_x1000.raw) / 1000.0f;
  }

  // Единый метод скорости в км/ч. ВСЕГДА конвертирует из узлов
  // (speed_kn_x1000), т.к. узлы - каноническая единица SOG в NMEA
  // (поле 7 RMC и поле 5 VTG). Для прямого значения из VTG (поле K)
  // используйте speed_kmh_from_vtg().
  [[nodiscard]] float speed_kmh() const noexcept {
    return static_cast<float>(speed_kn_x1000.raw) * 0.001852f;
  }

  [[nodiscard]] float speed_ms() const noexcept {
    return static_cast<float>(speed_kn_x1000.raw) * 0.00051444f;
  }

  // ЯВНЫЙ доступ к сырому значению км/ч, распарсенному непосредственно из
  // VTG (поле 7, суффикс K). Отличается от speed_kmh(): это не конверсия,
  // а значение, переданное приёмником; может отсутствовать, если VTG без поля K.
  [[nodiscard]] float speed_kmh_from_vtg() const noexcept {
    return static_cast<float>(speed_kmh_x1000.raw) / 1000.0f;
  }

  [[nodiscard]] float course_true_deg() const noexcept {
    return static_cast<float>(course_true.raw) / 100.0f; // course_true хранится как e2
  }

  [[nodiscard]] float course_mag_deg() const noexcept {
    return static_cast<float>(course_mag.raw) / 100.0f;
  }

  [[nodiscard]] float altitude_m() const noexcept {
    return static_cast<float>(altitude_cm) / 100.0f;
  }

  [[nodiscard]] float hdop_value() const noexcept {
    return static_cast<float>(hdop_e2) / 100.0f;
  }

  // === Сброс (обновлен под Type-Safe) ===

  // Сброс 2D-данных и времени (источники: RMC, GLL).
  // НЕ трогает 3D-данные GGA: невалидный RMC/GLL не должен стирать
  // корректную высоту/спутники/HDOP из независимого сообщения GGA.
  void reset_rmc() noexcept {
    valid = false;
    lat = Latitude();
    lon = Longitude();
    speed_kn_x1000 = Speed();
    course_true = Heading();
    time_utc = 0;
    date = 0;
    fix_mode = FixMode::NotValid;
    has_lat_lon = false;
    has_speed_course = false;
  }

  // Сброс 3D-данных (источник: GGA).
  // НЕ трогает 2D-данные RMC/GLL: невалидная GGA не должна стирать
  // валидную 2D-позицию/курс/скорость из независимых сообщений.
  void reset_gga() noexcept {
    altitude_cm = 0;
    satellites = 0;
    hdop_e2 = 0;
    has_altitude = false;
  }

  void reset_vtg() noexcept {
    course_true = Heading();
    course_mag = Heading();
    speed_kn_x1000 = Speed();
    speed_kmh_x1000 = Speed();
    has_vtg_speed = has_vtg_course = false;
  }

  void reset_gll() noexcept {
    lat = Latitude();
    lon = Longitude();
    time_utc = 0;
    valid = false;
    has_lat_lon = false;
  }

  void reset_all() noexcept {
    reset_rmc();
    reset_gga();
    reset_vtg();
    reset_gll();
    constellation = Constellation::Unknown;
    msg_type = MessageType::Unknown;
  }

  // === Проверки наличия данных ===
  [[nodiscard]] bool has_coordinates() const noexcept { return has_lat_lon; }
  [[nodiscard]] bool has_navigation() const noexcept { return has_lat_lon && has_speed_course; }
  [[nodiscard]] bool has_3d_fix() const noexcept { return has_lat_lon && has_altitude; }
  
  // Новая type-safe проверка
  // Единый источник истины - Coordinate::is_valid(): проверяет и диапазоны,
  // и ненулевые координаты (отвергает "нулевой остров" 0,0).
  [[nodiscard]] bool has_valid_position() const noexcept {
      return valid && Coordinate{lat, lon}.is_valid();
  }
};

// ============================================================================
// Класс парсера
// ============================================================================
class Parser {
public:
  struct Config {
    bool trust_gga_fix;
    bool enable_diagnostics;
    uint32_t cst_mask;

    Config(bool trust_gga = false, bool diagnostics = false, uint32_t mask = CST_MASK_ALL) noexcept
        : trust_gga_fix{trust_gga}, enable_diagnostics{diagnostics}, cst_mask{mask} {}
  };

  explicit Parser(Config cfg = Config{}) noexcept : config_{cfg} {}

  [[nodiscard]] bool parse_line(std::string_view line) noexcept;

  [[nodiscard]] const NMEAData &data() const noexcept { return data_; }
  [[nodiscard]] NMEAData &data() noexcept { return data_; }

  void set_cst_filter(uint32_t mask) noexcept { config_.cst_mask = mask; }

  struct Diagnostics {
    uint32_t reject_crc = 0;
    uint32_t reject_unknown_cst = 0;
    uint32_t reject_filtered_cst = 0;
    uint32_t reject_unknown_msg = 0;
    uint32_t reject_too_short = 0;
  };

  [[nodiscard]] const Diagnostics &diagnostics() const noexcept { return diag_; }
  void reset_diagnostics() noexcept { diag_ = {}; }

private:
  Config config_;
  NMEAData data_;
  Diagnostics diag_;

  std::array<uint8_t, MAX_COMMAS> comma_pos_{};
  uint8_t comma_count_ = 0;

  [[nodiscard]] bool scan_and_check_crc(std::string_view line, size_t &star_idx) noexcept;
  [[nodiscard]] static Constellation get_constellation(char b1, char b2) noexcept;

  [[nodiscard]] bool parse_rmc(std::string_view line, size_t star_idx) noexcept;
  [[nodiscard]] bool parse_gga(std::string_view line, size_t star_idx) noexcept;
  [[nodiscard]] bool parse_vtg(std::string_view line, size_t star_idx) noexcept;
  [[nodiscard]] bool parse_gll(std::string_view line, size_t star_idx) noexcept;

  [[nodiscard]] static constexpr uint32_t parse_uint32(std::string_view raw) noexcept;

  // Возвращает bool, а значение записывает в int32_t&
  [[nodiscard]] static constexpr bool parse_degrees_e7(std::string_view raw, char direction, int32_t &out) noexcept;
  // Единый парсер десятичной дроби без FPU. Масштаб - шаблонный параметр
  // FRAC_SCALE (10/100/1000 = 1/2/3 знака после запятой), поэтому все
  // арифметические константы известны на этапе компиляции.
  template <int32_t FRAC_SCALE>
  [[nodiscard]] static constexpr int32_t parse_fixed_point(std::string_view raw) noexcept;
  [[nodiscard]] static constexpr int32_t parse_value_e1(std::string_view raw) noexcept; // обёртка: масштаб x10
  [[nodiscard]] static constexpr int32_t parse_value_e2(std::string_view raw) noexcept; // обёртка: масштаб x100
  [[nodiscard]] static constexpr int32_t parse_value_e3(std::string_view raw) noexcept; // обёртка: масштаб x1000

  [[nodiscard]] std::string_view get_field(std::string_view line, uint8_t index) const noexcept;

  // Для парсинга координат
  LIGHT_NMEA_ALWAYS_INLINE
  bool parse_coordinates(
      std::string_view line,
      size_t start_idx) noexcept 
  {
    // Направления обязательны: только N/S для широты и E/W для долготы.
    // null-поле направления (5.2.2.3) означает "данные недоступны" и не
    // должно превращаться в валидный фикс.
    const std::string_view lat_dir = get_field(line, start_idx + 1);
    if (lat_dir.size() != 1 ||
        (lat_dir[0] != CHAR_N && lat_dir[0] != CHAR_S)) {
      return false;
    }
    const std::string_view lon_dir = get_field(line, start_idx + 3);
    if (lon_dir.size() != 1 ||
        (lon_dir[0] != CHAR_E && lon_dir[0] != CHAR_W)) {
      return false;
    }

    int32_t lat_raw = 0;
    if (!parse_degrees_e7(get_field(line, start_idx), lat_dir[0], lat_raw)) {
      return false;
    }

    int32_t lon_raw = 0;
    if (!parse_degrees_e7(get_field(line, start_idx + 2), lon_dir[0], lon_raw)) {
      return false;
    }

    data_.lat = Latitude(lat_raw);
    data_.lon = Longitude(lon_raw);
    data_.has_lat_lon = true;
    return true;
  }
};

// ============================================================================
// Реализация
// ============================================================================

inline bool Parser::parse_line(std::string_view line) noexcept {
  if (line.size() < MIN_PACKET_LEN || line.size() > MAX_PACKET_SIZE) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_too_short++;
    return false;
  }
  if (line[0] != CHAR_DOLLAR) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_too_short++;
    return false;
  }

  // Адресное поле (5.2.1.1): ровно 5 символов, сразу за ним разделитель ",".
  // Класс символов отдельно не проверяю: talker (байты 1-2) сверяется с таблицей
  // созвездий ниже, а форматтер (байты 3-5) - точным сравнением с
  // RMC/GGA/VTG/GLL. MIN_PACKET_LEN >= 10 гарантирует доступность line[6].
  if (line[6] != CHAR_COMMA) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_unknown_msg++;
    return false;
  }

  // Ранняя отбраковка до O(n) сканирования в поиске CRC
  // Проверяет созвездие (talker), фильтр и тип сообщения по первым байтам,
  // чтобы не тратить полный проход CRC+запятые на чужие/неподдерживаемые кадры/пакеты/сентенции.
  // В data_ пишу только после успешной проверки CRC (сохраняет "последние валидные данные").
  const Constellation cst = get_constellation(line[1], line[2]);
  if (cst == Constellation::Unknown) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_unknown_cst++;
    return false;
  }
  if (!(config_.cst_mask & (1u << to_underlying(cst)))) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_filtered_cst++;
    return false;
  }

  MessageType mt = MessageType::Unknown;
  const char t1 = line[3], t2 = line[4], t3 = line[5];
  if (t1 == CHAR_R && t2 == CHAR_M && t3 == CHAR_C) LIGHT_NMEA_LIKELY {
    mt = MessageType::RMC;
  } else if (t1 == CHAR_G && t2 == CHAR_G && t3 == CHAR_A) LIGHT_NMEA_LIKELY {
    mt = MessageType::GGA;
  } else if (t1 == CHAR_V && t2 == CHAR_T && t3 == CHAR_G) LIGHT_NMEA_LIKELY {
    mt = MessageType::VTG;
  } else if (t1 == CHAR_G && t2 == CHAR_L && t3 == CHAR_L) LIGHT_NMEA_LIKELY {
    mt = MessageType::GLL;
  }
  if (mt == MessageType::Unknown) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_unknown_msg++;
    return false;
  }

  size_t star_idx = 0;
  if (!scan_and_check_crc(line, star_idx)) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_crc++;
    return false;
  }

  data_.constellation = cst;
  data_.msg_type = mt;

  switch (mt) {
    case MessageType::RMC: return parse_rmc(line, star_idx);
    case MessageType::GGA: return parse_gga(line, star_idx);
    case MessageType::VTG: return parse_vtg(line, star_idx);
    case MessageType::GLL: return parse_gll(line, star_idx);
    default:               return false; // недостижимо (Unknown отклонён выше)
  }
}

LIGHT_NMEA_ALWAYS_INLINE bool Parser::scan_and_check_crc(std::string_view line, size_t &star_idx) noexcept {
  uint8_t calc_cs = 0;
  star_idx = 0;
  comma_count_ = 0;

  for (size_t i = 1; i < line.size(); ++i) {
    const char c = line[i];
    if (c == CHAR_STAR) {
      star_idx = i;
      break;
    }
    if (c == CHAR_COMMA) {
      if (comma_count_ < MAX_COMMAS) {
        comma_pos_[comma_count_++] = static_cast<uint8_t>(i);
      }
    }
    calc_cs ^= static_cast<uint8_t>(c);
  }

  if (star_idx == 0 || star_idx + 3 > line.size()) LIGHT_NMEA_UNLIKELY return false;

  auto hex_to_int = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };

  const int n1 = hex_to_int(line[star_idx + 1]);
  const int n2 = hex_to_int(line[star_idx + 2]);
  if (n1 < 0 || n2 < 0) LIGHT_NMEA_UNLIKELY return false;

  return calc_cs == static_cast<uint8_t>((n1 << 4) | n2);
}

inline Constellation Parser::get_constellation(char b1, char b2) noexcept {
  if (b1 == CHAR_G && b2 >= 'A' && b2 <= 'Q') {
    return detail::CST_LOOKUP_BY_SECOND_BYTE[b2 - 'A'];
  }
  if (b1 == 'B' && b2 == 'D') return Constellation::BeiDou;
  if (b1 == 'Q' && b2 == 'Z') return Constellation::QZSS;
  if (b1 == 'I' && b2 == 'R') return Constellation::NavIC;
  return Constellation::Unknown;
}

LIGHT_NMEA_ALWAYS_INLINE constexpr uint32_t Parser::parse_uint32(std::string_view raw) noexcept {
  if (raw.empty()) return 0;
  uint32_t result = 0;
  for (char c : raw) {
    if (c >= '0' && c <= '9') result = result * 10 + (c - '0');
  }
  return result;
}

LIGHT_NMEA_ALWAYS_INLINE constexpr bool Parser::parse_degrees_e7(std::string_view raw, char direction, int32_t &out) noexcept {
  constexpr int32_t SCALE_E7 = 10000000;  // Масштаб для координат (x10^7)
  constexpr size_t MAX_FRAC_DIGITS = 7;   // Максимальное количество знаков в дробной части

  // Единый проход слева направо (без find '.'). Угловые минуты - всегда ровно 2 цифры перед точкой,
  // поэтому целая часть накапливается как degrees*100 + minutes,
  // и граница вычисляется операциями %100 и /100.
  int32_t int_total = 0;   // Все цифры до точки = degrees*100 + minutes
  int32_t minutes_frac = 0;
  size_t int_digits = 0, frac_count = 0;
  bool seen_dot = false;

  for (char c : raw) {
    if (c == '.') {
      seen_dot = true;
    } else if (c >= '0' && c <= '9') {
      if (!seen_dot) {
        int_total = int_total * 10 + (c - '0');
        ++int_digits;
      } else if (frac_count < MAX_FRAC_DIGITS) {
        minutes_frac = minutes_frac * 10 + (c - '0');
        ++frac_count;
      }
    }
  }

  // Координаты (Таблица 6): ddmm.mm / dddmm.mm. Ведущие нули обязательны;
  // десятичная точка и дробная часть не обязательны, если полная точность не требуется.
  // Без точки нужно минимум 4 целых цифры (ddmm), чтобы отделить минуты (2 последние цифры) от градусов.
  if (int_digits < 2) return false;
  if (!seen_dot && int_digits < 4) return false;

  const int32_t minutes = int_total % 100;
  const int32_t degrees = int_total / 100;

  // Дополняю нулями справа до MAX_FRAC_DIGITS количества знаков
  while (frac_count < MAX_FRAC_DIGITS) {
    minutes_frac *= 10;
    ++frac_count;
  }

  // Преобразование угловых минут в градусы: degrees + minutes/60
  const uint32_t minutes_total =
      static_cast<uint32_t>(minutes) * SCALE_E7 + static_cast<uint32_t>(minutes_frac);
  const int32_t minutes_in_degrees = static_cast<int32_t>(minutes_total / 60);

  // Результат в масштабе 1e7
  int32_t result = degrees * SCALE_E7 + minutes_in_degrees;

  // Южное/Западное полушарие - отрицательные координаты
  if (direction == CHAR_S || direction == CHAR_W) {
    result = -result;
  }

  out = result;
  return true;
}

template <int32_t FRAC_SCALE>
LIGHT_NMEA_ALWAYS_INLINE constexpr int32_t Parser::parse_fixed_point(std::string_view raw) noexcept {
  if (raw.empty()) return 0;
  // Знак "-" (HEX 2D) может стоять первым символом поля (Таблица 6 NMEA-0183,
  // примечание 2), например отрицательная высота над уровнем моря (MSL) в GGA. Цикл ниже не
  // считает '-' цифрой, поэтому знак учитываю отдельно.
  const bool neg = (raw[0] == CHAR_MINUS);
  // Десятичная дробь с фиксированным масштабом FRAC_SCALE (x10/x100/x1000 = 1/2/3
  // знака после запятой; лишние знаки отбрасываются). Дробные цифры выравниваются
  // ОТ СТАРШЕЙ значащей цифры к фиксированному масштабу: "22.4", "22.40" и
  // "22.400" дают один raw (22400 при FRAC_SCALE=1000), "0.046" не теряет ноль.
  int32_t int_part = 0, frac = 0, frac_place = FRAC_SCALE / 10;
  bool after_dot = false;
  for (char c : raw) {
    if (c == '.') after_dot = true;
    else if (c >= '0' && c <= '9') {
      const int d = c - '0';
      if (!after_dot) int_part = int_part * 10 + d;
      else if (frac_place > 0) {
        frac += d * frac_place;
        // if constexpr: шаг масштаба сворачивается в постоянные,
        // как в прежних трёх специализированных функциях (без деления в рантайме).
        if constexpr (FRAC_SCALE == 10) {
          frac_place = 0;
        } else if constexpr (FRAC_SCALE == 100) {
          frac_place = (frac_place == 10) ? 1 : 0;
        } else {
          frac_place = (frac_place == 100) ? 10 : (frac_place == 10) ? 1 : 0;
        }
      }
    }
  }
  const int32_t value = int_part * FRAC_SCALE + frac;
  return neg ? -value : value;
}

LIGHT_NMEA_ALWAYS_INLINE constexpr int32_t Parser::parse_value_e1(std::string_view raw) noexcept {
  return parse_fixed_point<10>(raw);   // курс: 1 знак дроби -> x10 (далее *10 -> e2)
}

LIGHT_NMEA_ALWAYS_INLINE constexpr int32_t Parser::parse_value_e2(std::string_view raw) noexcept {
  return parse_fixed_point<100>(raw);  // HDOP, высота: 2 знака -> x100
}

LIGHT_NMEA_ALWAYS_INLINE constexpr int32_t Parser::parse_value_e3(std::string_view raw) noexcept {
  return parse_fixed_point<1000>(raw); // SOG: 3 знака -> x1000
}

LIGHT_NMEA_ALWAYS_INLINE std::string_view Parser::get_field(std::string_view line, uint8_t index) const noexcept {
  if (index >= comma_count_) LIGHT_NMEA_UNLIKELY return {};
  const size_t start = (index == 0) ? 0 : comma_pos_[index - 1] + 1;
  const size_t end = comma_pos_[index];
  return line.substr(start, end - start);
}


// ============================================================================
// RMC
// ============================================================================
inline bool Parser::parse_rmc(std::string_view line, size_t star_idx) noexcept {
  if (comma_count_ < 9) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_too_short++;
    return false;
  }

  // Парсинг fix_mode из Поля 12 (Mode Indicator, NMEA v3.01+).
  // По умолчанию считаю фикс невалидным.
  data_.fix_mode = FixMode::NotValid;
  // Mode Indicator есть только если запятых >= 12 (NMEA v3.01 и новее)
  if (comma_count_ >= 12) {
    // Начало Поля 12: сразу после 11-й запятой
    const size_t mode_st = comma_pos_[11] + 1;
    
    // Конец Поля 12:
    // Если есть Поле 13 (Navigational Status, comma_count_ >= 13), то конец - это 12-я запятая.
    // Иначе (NMEA v3.01), конец - это символ '*' (star_idx).
    const size_t mode_en = (comma_count_ >= 13) ? comma_pos_[12] : star_idx;

    if (mode_en > mode_st) {
      const char mode_byte = line[mode_st];
      if (mode_byte >= 'A' && mode_byte <= 'R') {
        data_.fix_mode = detail::FIX_MODE_TABLE[mode_byte - 'A'];
      } else {
        data_.fix_mode = FixMode::NotValid;
      }
    }
  } // if (comma_count_ >= 12) {

  const std::string_view status = get_field(line, 2);
  if (status.size() == 1 && status[0] == CHAR_A) LIGHT_NMEA_LIKELY {
    data_.valid = true;
    data_.time_utc = parse_uint32(get_field(line, 1).substr(0, 6));

    // Парсинг координат с помощью helper (координаты в RMC начинаются с поля 3)
    if (!parse_coordinates(line, 3)) {
      data_.reset_rmc();
      return false;
    }

    const std::string_view speed_raw = get_field(line, 7);
    if (!speed_raw.empty()) {
      data_.speed_kn_x1000 = Speed(parse_value_e3(speed_raw));
      data_.has_speed_course = true;
    }

    const std::string_view course_raw = get_field(line, 8);
    if (!course_raw.empty()) {
      data_.course_true = Heading(static_cast<int32_t>(parse_value_e1(course_raw)) * 10);
      data_.has_speed_course = true;
    }

    data_.date = parse_uint32(get_field(line, 9));
    return true;
  }

  data_.reset_rmc();
  return true;
}

// ============================================================================
// GGA
// ============================================================================
inline bool Parser::parse_gga(std::string_view line, size_t /*star_idx*/) noexcept {
  if (comma_count_ < 9) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_too_short++;
    return false;
  }

  const std::string_view fix_quality_sv = get_field(line, 6);
  
  if (!fix_quality_sv.empty() && fix_quality_sv[0] != CHAR_0) LIGHT_NMEA_LIKELY {
    data_.valid = true;

    // UTC позиции (поле 1): GGA-only потоки (без RMC/GLL) тоже получают время.
    // На невалидной GGA сентенции не стираю время - как и остальные 2D-данные.
    const std::string_view time_raw = get_field(line, 1);
    if (!time_raw.empty()) {
      data_.time_utc = parse_uint32(time_raw.substr(0, 6));
    }

    // Парсинг координат с помощью helper (координаты в GGA начинаются с поля 2)
    if (!parse_coordinates(line, 2)) {
      data_.reset_rmc();
      return false;
    }

    const std::string_view sats = get_field(line, 7);
    if (!sats.empty()) {
      data_.satellites = static_cast<uint8_t>(parse_uint32(sats));
    }

    const std::string_view hdop_raw = get_field(line, 8);
    if (!hdop_raw.empty()) {
      data_.hdop_e2 = static_cast<int16_t>(parse_value_e2(hdop_raw));
    }

    const std::string_view alt_raw = get_field(line, 9);
    if (!alt_raw.empty()) {
      data_.altitude_cm = parse_value_e2(alt_raw);
      data_.has_altitude = true;
    }

    if (config_.trust_gga_fix) {
      // parse_uint32 неотрицателен по строению, поэтому проверяем только верхнюю границу таблицы.
      const int fix_quality = static_cast<int>(parse_uint32(fix_quality_sv));
      if (fix_quality < static_cast<int>(detail::GGA_QUALITY_FIX_MODE.size())) {
        data_.fix_mode = detail::GGA_QUALITY_FIX_MODE[fix_quality];
      }
    }
  } else {
    data_.reset_gga();
  }
  
  return true;
}


// ============================================================================
// VTG
// ============================================================================
inline bool Parser::parse_vtg(std::string_view line, size_t /*star_idx*/) noexcept {
  if (comma_count_ < 8) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_too_short++;
    return false;
  }

  // Точечный сброс VTG-состояния: незаполненные поля нового кадра не должны
  // наследовать значения/флаги предыдущего.
  // course_true и speed_kn_x1000 намеренно не трогаю - они могут приходить из RMC.
  data_.has_vtg_speed = false;
  data_.has_vtg_course = false;
  data_.course_mag = Heading();
  data_.speed_kmh_x1000 = Speed();

  const std::string_view course_true_raw = get_field(line, 1);
  if (!course_true_raw.empty()) {
    data_.course_true = Heading(static_cast<int32_t>(parse_value_e1(course_true_raw)) * 10);
    data_.has_vtg_course = true;
  }

  const std::string_view course_mag_raw = get_field(line, 3);
  if (!course_mag_raw.empty()) {
    data_.course_mag = Heading(static_cast<int32_t>(parse_value_e1(course_mag_raw)) * 10);
  }

  const std::string_view speed_kn_raw = get_field(line, 5);
  if (!speed_kn_raw.empty()) {
    data_.speed_kn_x1000 = Speed(parse_value_e3(speed_kn_raw));
    data_.has_vtg_speed = true;
    data_.has_speed_course = true;
  }

  const std::string_view speed_kmh_raw = get_field(line, 7);
  if (!speed_kmh_raw.empty()) {
    data_.speed_kmh_x1000 = Speed(parse_value_e3(speed_kmh_raw));
  }

  return true;
}

// ============================================================================
// GLL
// ============================================================================
inline bool Parser::parse_gll(std::string_view line, size_t /*star_idx*/) noexcept {
  if (comma_count_ < 6) LIGHT_NMEA_UNLIKELY {
    if (config_.enable_diagnostics) diag_.reject_too_short++;
    return false;
  }

  // Проверяю статус. Сначала поле 6
  const std::string_view status = get_field(line, 6);
  if (status.size() < 1 || status[0] != CHAR_A) {
    data_.reset_rmc();
    return true;
  }

  data_.valid = true;

  // Парсинг координат (координаты в GLL начинаются с поля 1)
  if (!parse_coordinates(line, 1)) {
    data_.reset_rmc();
    return false;
  }

  // Парсинг времени (поле 5)
  const std::string_view time_raw = get_field(line, 5);
  if (!time_raw.empty()) {
    data_.time_utc = parse_uint32(time_raw.substr(0, 6));
  }

  return true;
}

} // namespace light_nmea
