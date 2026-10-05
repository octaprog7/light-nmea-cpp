/*
 * dash_utils.hpp - общие утилиты, модель данных и serial-парсер для
 * PC-инструментов GNSS (C++-аналог dash_utils.py).
 * Copyright (C) 2026 Roman Shevchik
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Модуль не зависит от curses и используется дашбордом (gnss_dashboard.cpp).
 * Содержит: константы, утилиты, модель GNSSData, LogWriter, AccuracyTracker
 * и SerialParser (последовательный порт + буфер + разбор CSV/NMEA/SYS_MSG).
 *
 * Потоки данных (как в Python-версии):
 *   - CSV: 12 полей, разделённых запятыми (поток от платы с MicroPython);
 *   - NMEA-0183: сентенции $XXYYY,...*HH;
 *   - SYS_MSG: системные сообщения микроконтроллера "SYS_MSG:<тип>:<данные>".
 */

#pragma once

#include "nmea_formatter.hpp"
#include "serial_stream.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <unistd.h>

namespace dash_utils {

// ============================================================================
// КОНСТАНТЫ (соответствуют dash_utils.py)
// ============================================================================

// Типы потоков данных для парсинга
enum DataStreamType : uint8_t {
  DATA_STREAM_UNKNOWN = 0,
  DATA_STREAM_CSV = 1,
  DATA_STREAM_NMEA_0183 = 2
};

// Общие
inline constexpr const char *PLACEHOLDER = "---"; // заполнитель отсутствующих значений
inline constexpr double TO_KMH = 1.0;  // скорость в потоках уже в км/ч
inline constexpr int COMMA_COUNT_CSV = 11;  // кол-во запятых в CSV-потоке (12 полей)
inline constexpr int CSV_FIELDS_COUNT = 12; // кол-во полей в CSV-потоке данных

// Serial / USB
inline constexpr uint32_t DEFAULT_BAUDRATE = 115200; // 38400
inline constexpr double SERIAL_TIMEOUT_S = 0.05;
inline constexpr double RECONNECT_DELAY_S = 2.0;
// Максимальный размер буфера строки от MCU (защита от разрастания при мусоре без '\n')
inline constexpr size_t MAX_BUFFER_SIZE = 4096;

// Системные сообщения от MicroPython (MCU)
inline constexpr const char *SYS_MSG_PREFIX = "SYS_MSG:";
inline constexpr int MCU_MSG_MODULE_DETECTED = 1;
inline constexpr int MCU_MSG_SOFTWARE_RESET = 2;
inline constexpr int MCU_MSG_WATCHDOG_TRIGGERED = 3;
inline constexpr const char *DEFAULT_MODULE_NAME = "Unknown";

// Логирование
inline constexpr const char *LOG_FILENAME = "gnss_log.csv";
inline constexpr const char *LOG_CSV_HEADER =
    "timestamp,valid,satellites,latitude,longitude,speed,course,altitude,"
    "time,date,constellation,fix_mode,hdop\n";

// Анализ точности
inline constexpr double STATIONARY_SPEED_KMH = 2.0;
inline constexpr double STATIONARY_TIME_S = 33.3;
inline constexpr double M_PER_DEG_LAT = 111320.0;
inline constexpr uint32_t MIN_POINTS_FOR_ACCURACY = 2;

// Компас
inline constexpr double FULL_CIRCLE_DEG = 360.0;
inline constexpr int COMPASS_DIVISIONS = 8;
inline constexpr double COMPASS_OFFSET_DEG = 22.5;
inline constexpr double COMPASS_SECTOR_DEG = 45.0;

inline constexpr double kPi = 3.14159265358979323846;

// ============================================================================
// УТИЛИТЫ
// ============================================================================

// Возвращает текущее время в секундах (monotonic) - аналог now() в python.
inline double now() noexcept {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

inline void sleep_ms(uint32_t ms) noexcept {
  timespec ts{};
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
  nanosleep(&ts, nullptr);
}

// Выводит сообщение в destination с временной меткой (аналог log_msg в python).
inline void log_msg(const std::string &message, FILE *destination) noexcept {
  char ts[32];
  const time_t t = ::time(nullptr);
  struct tm tmv {};
  localtime_r(&t, &tmv);
  strftime(ts, sizeof(ts), "%d-%m-%Y %H:%M:%S", &tmv);
  fprintf(destination, "[%s] %s\n", ts, message.c_str());
}

// Определяет тип порта по имени устройства (аналог get_port_type в python).
inline std::string get_port_type(const std::string &port) noexcept {
  return (port.find("ttyACM") != std::string::npos ||
          port.find("ttyUSB") != std::string::npos)
             ? "USB-UART"
             : "Serial";
}

// Форматирует скорость в км/ч (аналог format_speed в python).
// Используется std::to_chars: не зависит от локали (в ru_RU printf пишет
// запятую вместо точки, что ломает чтение).
inline std::string format_speed(double speed_kmh) noexcept {
  if (std::isnan(speed_kmh))
    return std::string(PLACEHOLDER) + " km/h";
  char buf[32];
  auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), speed_kmh,
                               std::chars_format::fixed, 1);
  (void)ec;
  return std::string(buf, p) + " km/h";
}

// Аналог _is_nmea_0183 в python: проверяет, является ли строка корректной
// сентенцией NMEA-0183:
//   - префикс '$' или '!';
//   - CRLF на конце отбрасывается;
//   - контрольная сумма '*HH', если присутствует, обязана быть в самом конце
//     и состоять из двух hex-цифр;
//   - идентификатор сентенции (talker + тип) - не менее 4 символов [A-Za-z0-9].
inline bool is_nmea_0183(const std::string &line) noexcept {
  const size_t n = line.size();
  if (n < 5)
    return false;

  const char c0 = line[0];
  if (c0 != '$' && c0 != '!')
    return false;

  // отбрасываю CRLF
  size_t end = n;
  if (line[end - 1] == '\n')
    --end;
  if (end > 1 && line[end - 1] == '\r')
    --end;

  // тело без префикса и без переводов строк внутри
  const size_t body_len = end - 1;
  const char *body = line.data() + 1;
  for (size_t i = 0; i < body_len; ++i) {
    if (body[i] == '\r' || body[i] == '\n')
      return false;
  }

  // контрольная сумма *HH
  const auto is_hex = [](char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
  };
  size_t data_end = body_len;
  for (size_t i = body_len; i-- > 0;) {
    if (body[i] == '*') {
      if (i + 2 >= body_len)
        return false; // после '*' меньше двух hex-цифр
      if (!is_hex(body[i + 1]) || !is_hex(body[i + 2]))
        return false;
      if (i + 3 != body_len)
        return false; // '*HH' должен быть в самом конце
      data_end = i;
      break;
    }
  }

  // идентификатор сентенции: всё до первой запятой (или до конца),
  // не менее 4 символов [A-Za-z0-9]
  size_t id_end = data_end;
  for (size_t i = 0; i < data_end; ++i) {
    if (body[i] == ',') {
      id_end = i;
      break;
    }
  }

  if (id_end < 4)
    return false;

  for (size_t i = 0; i < id_end; ++i) {
    const char c = body[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9')))
      return false;
  }

  return true;
}

// Аналог _is_csv в python: строка не начинается с '$'/'!' и содержит ровно
// csv_fields_count - 1 запятых (12 полей -> 11 запятых).
inline bool is_csv(const std::string &line, int csv_fields_count) noexcept {
  if (line.empty())
    return false;
  const char c0 = line[0];
  if (c0 == '$' || c0 == '!')
    return false;
  int commas = 0;
  for (char c : line)
    if (c == ',')
      ++commas;
  return commas == csv_fields_count - 1;
}

// Определяет формат данных в line (аналог detect_format в python):
//   0 - неизвестный, 1 - CSV, 2 - NMEA-0183.
// Сначала строго проверяется NMEA-0183, затем CSV.
inline DataStreamType detect_format(
    const std::string &line,
    int csv_fields_count = CSV_FIELDS_COUNT) noexcept {
  if (line.empty())
    return DATA_STREAM_UNKNOWN;
  if (is_nmea_0183(line))
    return DATA_STREAM_NMEA_0183;
  if (is_csv(line, csv_fields_count))
    return DATA_STREAM_CSV;
  return DATA_STREAM_UNKNOWN;
}

// Человеко-читаемое строковое наименование формата потока данных по его
// идентификатору, возвращаемому функцией detect_format (аналог
// stream_format_by_id и _FMT_STREAM в python): "Unknwn", "CSV", "NMEA-0183".
inline const char *stream_format_by_id(DataStreamType id_fmt) noexcept {
  switch (id_fmt) {
  case DATA_STREAM_CSV:
    return "CSV";
  case DATA_STREAM_NMEA_0183:
    return "NMEA-0183";
  default:
    return "Unknwn";
  }
}

// Код производителя расширения NMEA-0183 (аналог get_mfr_code в python).
// Возвращает 0 для стандартной сентенции без расширений производителей.
// Для проприетарной сентенции ($P...): 3 младших байта результата - это
// ASCII-коды 3-буквенного кода вендора: UBX, GRM, TNL и т.д.
// Пример: $PUBX,00 -> 0x554258 (UBX); $PGRMZ -> 0x47524D (GRM);
//         $PTNL    -> 0x544E4C (TNL).
// Внимание: в python-версии get_mfr_code сдвиги <<24/<<16/<<8 не согласованы
// с извлечением в code_to_mfr_string (>>16/>>8/>>0) - круговая конвертация
// даёт "BX\0" вместо "UBX". Здесь реализовано документированное поведение,
// при котором code_to_mfr_string(get_mfr_code("$PUBX")) == "UBX".
inline uint32_t get_mfr_code(const std::string &sentence) noexcept {
  const size_t n = sentence.size();
  if (n < 5)
    return 0;
  if (sentence[0] != '$' || sentence[1] != 'P')
    return 0;

  // позиция запятой или звёздочки - конец идентификатора
  const size_t comma_pos = sentence.find(',', 2);
  const size_t star_pos = sentence.find('*', 2);

  size_t end = n;
  if (comma_pos != std::string::npos &&
      (star_pos == std::string::npos || comma_pos < star_pos)) {
    end = comma_pos;
  } else if (star_pos != std::string::npos) {
    end = star_pos;
  }

  if (end < 5) // нужно минимум $P + 3 символа производителя
    return 0;

  // производитель - 3 символа начиная с позиции 2
  const auto b = [](char c) {
    return static_cast<uint32_t>(static_cast<unsigned char>(c));
  };
  return (b(sentence[2]) << 16) | (b(sentence[3]) << 8) | b(sentence[4]);
}

// Преобразует число, возвращённое get_mfr_code(), в строку краткого имени
// производителя расширения NMEA-0183: 'UBX', 'GRM', 'TNL' (аналог
// code_to_mfr_string в python). Для 0 возвращает пустую строку.
inline std::string code_to_mfr_string(uint32_t code) noexcept {
  if (code == 0)
    return "";

  // достаём три байта: старший, средний и младший
  const char b1 = static_cast<char>((code >> 16) & 0xFF);
  const char b2 = static_cast<char>((code >> 8) & 0xFF);
  const char b3 = static_cast<char>(code & 0xFF);
  return std::string() + b1 + b2 + b3;
}

// Форматирует время или дату из NMEA в человекочитаемый вид
// (аналог format_nmea_datetime в python): время "HH:MM:SS", дата "DD.MM.20YY".
inline std::string format_nmea_datetime(uint32_t value, bool is_time) noexcept {
  if (value == 0)
    return "";
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%06u", value); // "012345" / "010226"
  std::string s(tmp);
  if (s.size() > 6)
    s.resize(6); // берём первые 6 цифр (HHMMSS / DDMMYY)
  if (is_time)
    return s.substr(0, 2) + ":" + s.substr(2, 2) + ":" + s.substr(4, 2);
  return s.substr(0, 2) + "." + s.substr(2, 2) + ".20" + s.substr(4, 2);
}

// Автоопределение последовательного порта платы (аналог detect_port в python).
// Ищет /dev/ttyACM* и /dev/ttyUSB*. Возвращает true при успехе.
inline bool detect_port(std::string &out) noexcept {
  for (int i = 0; i < 16; ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "/dev/ttyACM%d", i);
    if (::access(buf, F_OK) == 0) {
      out = buf;
      return true;
    }
  }
  for (int i = 0; i < 16; ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "/dev/ttyUSB%d", i);
    if (::access(buf, F_OK) == 0) {
      out = buf;
      return true;
    }
  }
  return false;
}

// Парсит аргументы командной строки: DEVICE [BAUD] (аналог parse_args в python).
// При некорректном BAUD выводит сообщение и возвращает false.
inline bool parse_args(int argc, char **argv, std::string &port,
                       uint32_t &baudrate) noexcept {
  if (argc > 1)
    port = argv[1];
  if (argc > 2) {
    const long b = strtol(argv[2], nullptr, 10);
    if (b > 0) {
      baudrate = static_cast<uint32_t>(b);
    } else {
      log_msg(std::string("Invalid baudrate: ") + argv[2], stderr);
      return false;
    }
  }
  return true;
}

// Сторона света по курсу (угол в градусах) - как GNSSData.compass_letter.
inline const char *compass_letter(double course) noexcept {
  static const char *dirs[] = {"N", "NE", "E", "SE",
                               "S", "SW", "W", "NW"};
  double v = fmod(course, FULL_CIRCLE_DEG);
  if (v < 0)
    v += FULL_CIRCLE_DEG;
  const int idx = static_cast<int>((v + COMPASS_OFFSET_DEG) /
                                   COMPASS_SECTOR_DEG) %
                  COMPASS_DIVISIONS;
  return dirs[idx];
}

// Валидные имена режимов фикса (как is_valid_fix_name в conv_to_hrf.py).
inline bool is_valid_fix_name(const std::string &name) noexcept {
  return name == "Autonomous" || name == "DGPS" || name == "RTK Fixed" ||
         name == "RTK Float";
}

// ============================================================================
// GNSSData - одна строка данных от GNSS-приёмника (аналог GNSSData в python)
// ============================================================================
struct GNSSData {
  bool valid = false;
  int satellites = -1;     // -1 = неизвестно
  double latitude = NAN;   // NAN = отсутствует
  double longitude = NAN;
  double speed_kmh = NAN;  // скорость в км/ч (как в CSV-потоке платы)
  double course = NAN;
  double altitude = NAN;
  double hdop = NAN;
  std::string time;           // "HH:MM:SS" или ""
  std::string date;           // "DD.MM.20YY" или ""
  std::string constellation;  // имя ("GPS", "GLONASS", ...)
  std::string fix_mode;       // имя ("Autonomous", "Not Valid", ...)

  // Парсит CSV-строку платы (12 полей). Возвращает false при неверном числе
  // полей (аналог ValueError в python).
  static bool from_csv(const std::string &line, GNSSData &out) noexcept;

  // Заполняет поля из данных NMEA-парсера (аналог GNSSData.from_parser).
  static GNSSData from_parser(const light_nmea::NMEAData &d) noexcept;

  // Сериализует поля в CSV-строку (12 полей, без timestamp).
  // Отсутствующие значения - пустая строка, спутники '0', valid '1'/'0'.
  std::string to_csv_line() const noexcept;
};

inline bool GNSSData::from_csv(const std::string &line, GNSSData &out) noexcept {
  std::string fields[CSV_FIELDS_COUNT];
  size_t n = 0;
  size_t start = 0;
  for (size_t i = 0; i <= line.size() && n < CSV_FIELDS_COUNT; ++i) {
    if (i == line.size() || line[i] == ',') {
      fields[n++] = line.substr(start, i - start);
      start = i + 1;
    }
  }
  if (n != CSV_FIELDS_COUNT)
    return false;

  // Парсинг double без зависимости от локали! В ru_RU.UTF-8 strtod считает
  // десятичным разделителем ЗАПЯТУЮ, останавливается на точке "." и
  // превращает "56.386344" в 56.0 -> на экране "56,000000". std::from_chars
  // всегда использует точку.
  auto to_double = [](const std::string &s) -> double {
    if (s.empty())
      return NAN;
    double v = NAN;
    const char *begin = s.data();
    const char *end = begin + s.size();
    auto [p, ec] = std::from_chars(begin, end, v);
    (void)p;
    if (ec != std::errc())
      return NAN;
    return v;
  };
  auto to_int = [](const std::string &s) -> int {
    return s.empty() ? -1 : static_cast<int>(strtol(s.c_str(), nullptr, 10));
  };

  out.valid = (fields[0] == "1");
  out.satellites = to_int(fields[1]);
  out.latitude = to_double(fields[2]);
  out.longitude = to_double(fields[3]);
  out.speed_kmh = to_double(fields[4]);
  out.course = to_double(fields[5]);
  out.altitude = to_double(fields[6]);
  out.time = fields[7];
  out.date = fields[8];
  out.constellation = fields[9];
  out.fix_mode = fields[10];
  out.hdop = to_double(fields[11]);
  return true;
}

inline GNSSData GNSSData::from_parser(const light_nmea::NMEAData &d) noexcept {
  GNSSData out;
  out.valid = d.valid;
  out.satellites =
      (d.satellites > 0) ? static_cast<int>(d.satellites) : -1;
  if (d.has_lat_lon) {
    out.latitude = d.latitude_deg();
    out.longitude = d.longitude_deg();
  }
  if (d.has_speed_course) {
    out.speed_kmh = d.speed_kmh();
    out.course = d.course_true_deg();
  }
  if (d.has_altitude)
    out.altitude = d.altitude_m();
  out.time = format_nmea_datetime(d.time_utc, true);
  out.date = format_nmea_datetime(d.date, false);
  // Парсер хранит индекс созвездия/фикса - приводим к имени как в CSV-потоке
  out.constellation = light_nmea::detail::get_name(
      light_nmea::detail::CST_NAMES, static_cast<uint8_t>(d.constellation));
  out.fix_mode = light_nmea::detail::get_name(
      light_nmea::detail::FIX_NAMES, static_cast<uint8_t>(d.fix_mode));
  if (d.hdop_e2 > 0)
    out.hdop = d.hdop_value();
  return out;
}

inline std::string GNSSData::to_csv_line() const noexcept {
  char lat[32] = "", lon[32] = "", spd[32] = "", crs[32] = "", alt[32] = "",
       hd[32] = "";
  if (!std::isnan(latitude))
    snprintf(lat, sizeof(lat), "%.6f", latitude);
  if (!std::isnan(longitude))
    snprintf(lon, sizeof(lon), "%.6f", longitude);
  if (!std::isnan(speed_kmh))
    snprintf(spd, sizeof(spd), "%.1f", speed_kmh);
  if (!std::isnan(course))
    snprintf(crs, sizeof(crs), "%.1f", course);
  if (!std::isnan(altitude))
    snprintf(alt, sizeof(alt), "%.1f", altitude);
  if (!std::isnan(hdop))
    snprintf(hd, sizeof(hd), "%.2f", hdop);

  char sats[16];
  snprintf(sats, sizeof(sats), "%d", satellites > 0 ? satellites : 0);

  std::string out;
  out.reserve(160);
  out += valid ? "1" : "0";
  out += ','; out += sats;
  out += ','; out += lat;
  out += ','; out += lon;
  out += ','; out += spd;
  out += ','; out += crs;
  out += ','; out += alt;
  out += ','; out += time;
  out += ','; out += date;
  out += ','; out += constellation;
  out += ','; out += fix_mode;
  out += ','; out += hd;
  return out;
}

// ============================================================================
// LogWriter - запись строк GNSS-данных в CSV-файл с timestamp (UTC)
// ============================================================================
// Аналог LogWriter в python: формат потока определяется автоматически через
// detect_format():
//   - CSV-поток пишется в лог напрямую;
//   - NMEA-0183 пишется через GNSSData::to_csv_line() (data не должен быть
//     nullptr - иначе строка пропускается);
//   - неизвестный формат пропускается (чтобы не нарушать CSV-контракт лога).
class LogWriter {
public:
  explicit LogWriter(const char *filename = LOG_FILENAME) noexcept
      : name_(filename ? filename : LOG_FILENAME) {
    open();
  }

  ~LogWriter() { close(); }

  LogWriter(const LogWriter &) = delete;
  LogWriter &operator=(const LogWriter &) = delete;

  // Открывает файл лога в режиме дополнения; в новый/пустой файл
  // записывает заголовок CSV.
  void open() noexcept {
    close();
    f_ = std::fopen(name_.c_str(), "a");
    if (!f_) {
      writing_ = false;
      return;
    }
    if (std::ftell(f_) == 0) {
      std::fputs(LOG_CSV_HEADER, f_);
      std::fflush(f_);
    }
    writing_ = true;
  }

  // Переоткрывает лог в другом файле.
  void reopen(const char *filename) noexcept {
    name_ = (filename && *filename) ? filename : LOG_FILENAME;
    open();
  }

  void close() noexcept {
    writing_ = false;
    if (f_) {
      std::fclose(f_);
      f_ = nullptr;
    }
  }

  [[nodiscard]] bool is_open() const noexcept { return f_ != nullptr; }
  [[nodiscard]] bool is_writing() const noexcept { return writing_ && f_; }
  [[nodiscard]] uint64_t lines_written() const noexcept { return count_; }
  void reset_count() noexcept { count_ = 0; }

  // Записывает строку данных GNSS в лог с UTC-временной меткой.
  void write(const std::string &raw_line, const GNSSData *data = nullptr) noexcept {
    if (!f_) {
      writing_ = false;
      return;
    }

    std::string record;
    switch (detect_format(raw_line)) {
    case DATA_STREAM_CSV:
      record = raw_line;
      break;
    case DATA_STREAM_NMEA_0183:
      if (!data)
        return; // NMEA распарсилась, но валидных данных нет - пропускаем
      record = data->to_csv_line();
      break;
    default:
      return; // неизвестный формат - пропускаем
    }

    char ts[16];
    const time_t t = ::time(nullptr);
    struct tm tmv {};
    gmtime_r(&t, &tmv);
    strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);

    if (std::fprintf(f_, "%s,%s\n", ts, record.c_str()) < 0) {
      writing_ = false;
      return;
    }
    std::fflush(f_);
    count_++;
    writing_ = true;
  }

private:
  std::string name_;
  FILE *f_ = nullptr;
  uint64_t count_ = 0;
  bool writing_ = false;
};

// ============================================================================
// AccuracyTracker - расчёт точности GNSS по алгоритму Уэлфорда
// ============================================================================
struct AccuracyMetrics {
  uint32_t n = 0;
  double valid_pct = 0.0;
  double avg_hdop = 0.0;
  double err_2d_m = 0.0;
  double drift_m = 0.0;
  double std_lat = 0.0;
  double std_lon = 0.0;
  double min_lat = 0.0;
  double max_lat = 0.0;
  double min_lon = 0.0;
  double max_lon = 0.0;
};

class AccuracyTracker {
public:
  void reset() noexcept { *this = AccuracyTracker(); }

  // Добавляет точку с координатами в выборку и обновляет статистику.
  // Точки без координат (NAN) и последовательные дубликаты координат
  // (повторный фикс от модуля, квантованный до одной и той же позиции)
  // игнорируются - они не несут информации о разбросе позиционирования.
  void add_point(const GNSSData &data) noexcept {
    if (std::isnan(data.latitude) || std::isnan(data.longitude))
      return;

    const double lat = data.latitude;
    const double lon = data.longitude;
    // Пропускаем дубликат: та же позиция, что и в последней добавленной точке
    if (n_ > 0 && lat == last_lat_ && lon == last_lon_)
      return;

    n_++;

    if (n_ == 1) {
      first_lat_ = last_lat_ = min_lat_ = max_lat_ = lat;
      first_lon_ = last_lon_ = min_lon_ = max_lon_ = lon;
    } else {
      last_lat_ = lat;
      last_lon_ = lon;
      if (lat < min_lat_)
        min_lat_ = lat;
      if (lat > max_lat_)
        max_lat_ = lat;
      if (lon < min_lon_)
        min_lon_ = lon;
      if (lon > max_lon_)
        max_lon_ = lon;
    }

    const double dlat = lat - mean_lat_;
    const double dlon = lon - mean_lon_;
    mean_lat_ += dlat / n_;
    mean_lon_ += dlon / n_;
    const double dlat2 = lat - mean_lat_;
    const double dlon2 = lon - mean_lon_;
    m2_lat_ += dlat * dlat2;
    m2_lon_ += dlon * dlon2;

    if (is_valid_fix_name(data.fix_mode))
      valid_fix_count_++;
    if (!std::isnan(data.hdop)) {
      hdop_sum_ += data.hdop;
      hdop_count_++;
    }
  }

  [[nodiscard]] uint32_t n() const noexcept { return n_; }

  // Возвращает рассчитанные метрики точности. Возвращает false, если точек
  // меньше MIN_POINTS_FOR_ACCURACY.
  bool get_metrics(AccuracyMetrics &out) const noexcept {
    if (n_ < MIN_POINTS_FOR_ACCURACY)
      return false;

    const double var_lat = m2_lat_ / static_cast<double>(n_ - 1);
    const double var_lon = m2_lon_ / static_cast<double>(n_ - 1);
    const double std_lat = sqrt(var_lat);
    const double std_lon = sqrt(var_lon);

    const double lat_rad = mean_lat_ * kPi / 180.0;
    const double m_per_deg_lon = M_PER_DEG_LAT * cos(lat_rad);

    const double err_lat_m = std_lat * M_PER_DEG_LAT;
    const double err_lon_m = std_lon * m_per_deg_lon;
    const double err_2d_m = sqrt(err_lat_m * err_lat_m + err_lon_m * err_lon_m);

    const double dlat_m = (last_lat_ - first_lat_) * M_PER_DEG_LAT;
    const double dlon_m = (last_lon_ - first_lon_) * m_per_deg_lon;
    const double drift_m = sqrt(dlat_m * dlat_m + dlon_m * dlon_m);

    const double valid_pct =
        (n_ > 0) ? 100.0 * static_cast<double>(valid_fix_count_) / n_ : 0.0;
    const double avg_hdop =
        (hdop_count_ > 0) ? hdop_sum_ / static_cast<double>(hdop_count_) : 0.0;

    out.n = n_;
    out.valid_pct = valid_pct;
    out.avg_hdop = avg_hdop;
    out.err_2d_m = err_2d_m;
    out.drift_m = drift_m;
    out.std_lat = std_lat;
    out.std_lon = std_lon;
    out.min_lat = min_lat_;
    out.max_lat = max_lat_;
    out.min_lon = min_lon_;
    out.max_lon = max_lon_;
    return true;
  }

private:
  uint32_t n_ = 0;
  double mean_lat_ = 0.0;
  double mean_lon_ = 0.0;
  double m2_lat_ = 0.0;
  double m2_lon_ = 0.0;
  double min_lat_ = 0.0;
  double max_lat_ = 0.0;
  double min_lon_ = 0.0;
  double max_lon_ = 0.0;
  double first_lat_ = 0.0;
  double first_lon_ = 0.0;
  double last_lat_ = 0.0;
  double last_lon_ = 0.0;
  uint32_t valid_fix_count_ = 0;
  double hdop_sum_ = 0.0;
  uint32_t hdop_count_ = 0;
};

// ============================================================================
// SerialParser - последовательный порт + буфер + разбор CSV/NMEA/SYS_MSG
// ============================================================================
// Аналог SerialParser в python (dash_utils.py): объединяет работу с
// последовательным портом и построчным буфером. Определяет формат каждой
// строки (CSV / NMEA-0183 / SYS_MSG) и преобразует её в GNSSData.
class SerialParser {
public:
  struct Result {
    bool has_data = false; // есть распарсенные данные (data)
    GNSSData data;
    bool is_error = false; // ошибка разбора строки (ValueError)
    std::string raw_line;  // исходная строка для логирования / SYS_MSG
    bool is_sys_msg = false; // raw_line - системное сообщение MCU
  };

  SerialParser() noexcept = default;
  ~SerialParser() { close(); }

  SerialParser(const SerialParser &) = delete;
  SerialParser &operator=(const SerialParser &) = delete;

  // Открывает порт и настраивает линии DTR/RTS ("пробуждение" USB-CDC
  // на контроллере RP2040). Возвращает true при успехе.
  bool open(const std::string &port, uint32_t baudrate) noexcept {
    close();
    port_ = port;
    baudrate_ = baudrate;
    if (!ser_.open(port.c_str(), baudrate))
      return false;
    ser_.set_dtr_rts(false, false);
    sleep_ms(100);
    ser_.set_dtr_rts(true, true);
    sleep_ms(500);
    ser_.flush_input();
    buffer_.clear();
    return true;
  }

  // Закрывает и заново открывает последовательный порт.
  bool reconnect() noexcept { return open(port_, baudrate_); }

  void close() noexcept {
    ser_.close();
    buffer_.clear();
  }

  [[nodiscard]] bool is_open() const noexcept { return ser_.is_open(); }
  [[nodiscard]] const std::string &port() const noexcept { return port_; }
  [[nodiscard]] uint32_t baudrate() const noexcept { return baudrate_; }

  // Тип потока данных, определённый функцией detect_format() для последней
  // распознанной строки (DATA_STREAM_CSV / DATA_STREAM_NMEA_0183).
  // Используется дашбордом для отображения в окне статуса.
  [[nodiscard]] DataStreamType stream_format() const noexcept {
    return last_format_;
  }

  // Код производителя GNSS-приёмника (расширение NMEA-0183, $P...) для
  // последней распознанной строки или 0 (аналог get_mfr_code в python).
  // Используется дашбордом для отображения в окне статуса.
  [[nodiscard]] uint32_t mfr_code() const noexcept { return mfr_code_; }

  // Читает данные из порта и возвращает распарсенную строку.
  // Накапливает байты во внутреннем буфере до символа перевода строки,
  // определяет формат данных (CSV или NMEA-0183) и преобразует их в
  // GNSSData. Системные сообщения MCU и нераспознанные строки возвращаются
  // как raw_line без парсинга.
  Result poll() noexcept {
    Result res;
    if (!ser_.is_open())
      return res;

    // Читаем все доступные байты
    uint8_t tmp[512];
    size_t n = 0;
    while ((n = ser_.read(tmp, sizeof(tmp))) > 0)
      buffer_.append(reinterpret_cast<const char *>(tmp), n);

    // Ищем конец строки
    const size_t nl = buffer_.find('\n');
    if (nl == std::string::npos) {
      // Защита от разрастания буфера: нет '\n' в пределах лимита - это мусор
      if (buffer_.size() > MAX_BUFFER_SIZE)
        buffer_.clear();
      return res;
    }

    std::string line = buffer_.substr(0, nl);
    buffer_.erase(0, nl + 1);

    // Очистка: отсекаем \r и пробелы по краям (как .strip() в python)
    size_t b = 0;
    while (b < line.size() && (line[b] == ' ' || line[b] == '\t' ||
                               line[b] == '\r' || line[b] == '\n'))
      ++b;
    size_t e = line.size();
    while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' ||
                     line[e - 1] == '\r' || line[e - 1] == '\n'))
      --e;
    line = line.substr(b, e - b);
    if (line.empty())
      return res;

    res.raw_line = line;

    // Системные сообщения MCU обрабатываются в дашборде
    if (line.find(SYS_MSG_PREFIX) != std::string::npos) {
      res.is_sys_msg = true;
      return res;
    }

    const DataStreamType fmt = detect_format(line);
    // запоминаю формат потока и код производителя в полях класса
    // (как в python: self._stream_format и self._mfr_code обновляются
    // для каждой строки, включая нераспознанные)
    last_format_ = fmt;
    mfr_code_ = get_mfr_code(line);
    switch (fmt) {
    case DATA_STREAM_CSV: {
      GNSSData d;
      if (GNSSData::from_csv(line, d)) {
        res.data = d;
        res.has_data = true;
      } else {
        res.is_error = true; // ValueError
      }
      return res;
    }
    case DATA_STREAM_NMEA_0183: {
      // Разбор NMEA-0183 отдельным парсером
      if (parser_.parse_line(line)) {
        const light_nmea::NMEAData &pd = parser_.data();
        if (pd.has_coordinates() && pd.hdop_e2 > 0) {
          res.data = GNSSData::from_parser(pd);
          res.has_data = true;
        }
      }
      // Если NMEA распарсился, но координат нет - возвращаем None, но
      // строку для логирования оставляем (лог сам её отфильтрует)
      return res;
    }
    default:
      return res; // неизвестный формат
    }
  }

private:
  light_nmea::SerialStream ser_;
  light_nmea::Parser parser_{
      light_nmea::Parser::Config(true, true, light_nmea::CST_MASK_ALL)};
  std::string buffer_;
  std::string port_;
  uint32_t baudrate_ = DEFAULT_BAUDRATE;
  DataStreamType last_format_ = DATA_STREAM_UNKNOWN;
  uint32_t mfr_code_ = 0; // код производителя расширения NMEA-0183 или 0
};

} // namespace dash_utils