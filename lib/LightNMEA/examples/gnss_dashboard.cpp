/*
 * gnss_dashboard.cpp - GNSS-дашборд (TUI) для Linux (Debian/Ubuntu).
 * Copyright (C) 2026 Roman Shevchik
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Переписано с gnss_dashboard.py + dash_utils.py на C++20.
 * Источник данных - последовательный порт (USB-CDC / USB-UART).
 *
 * Формат каждой строки определяется автоматически функцией detect_format()
 * (как в Python-версии):
 *   - подготовленный поток платы: CSV, 12 полей через запятую;
 *   - сырой поток NMEA-0183: сентенции $XXYYY,...*HH (light_nmea::Parser);
 *   - системные сообщения MCU: "SYS_MSG:<тип>:<данные>".
 *
 *   SerialPort -> SerialParser (построчный буфер + detect_format + Parser)
 *       -> GNSS Data -> GNSS Dashboard (TUI на ncursesw)
 *
 * Для сборки нужен пакет libncurses-dev (ncursesw):
 *   sudo apt install build-essential cmake libncurses-dev
 *
 * Сборка:
 *   cmake -S lib/LightNMEA -B lib/LightNMEA/out/build/release \
 *         -DCMAKE_BUILD_TYPE=Release
 *   cmake --build lib/LightNMEA/out/build/release --target gnss_dashboard -j
 *
 * Запуск:
 *   ./gnss_dashboard                      # автодетект USB-CDC @ 115200
 *   ./gnss_dashboard /dev/ttyACM0         # USB-CDC всегда 115200
 *   ./gnss_dashboard /dev/ttyUSB0 9600    # USB-UART адаптер
 *
 * Управление: q / Q / ESC - выход. Ошибки пишутся в mcu_debug.log,
 * данные - в gnss_log.csv.
 */

// Wide-char API ncursesw: макрос должен быть определён ДО включения curses.h.
#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#include <ncursesw/curses.h>

#include "dash_utils.hpp"

#include <algorithm>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

using namespace dash_utils;

namespace {

// ============================================================================
// Константы UI (соответствуют gnss_dashboard.py)
// ============================================================================
constexpr int UI_TIMEOUT_MS = 50;
constexpr int VK_ESCAPE = 27;
constexpr int DIVIDER_LENGTH = 20;
constexpr int FRAME_MARGIN = 2;
constexpr int TITLE_OFFSET_X = 2;
constexpr int LABEL_X_DEFAULT = 2; // Отступ метки по умолчанию
constexpr int CONTENT_START_Y = 2;

constexpr int MIN_TERM_HEIGHT = 24;
constexpr int MIN_TERM_WIDTH = 80;

// Активность логирования: если данных нет 10 секунд - считаем неактивным
constexpr double LOG_ACTIVITY_TIMEOUT_S = 10.0;

// Цвета
constexpr short COLOR_ERROR = 1;
constexpr short COLOR_OK = 2;

const std::wstring PLACEHOLDER_W = L"---";

// Цветовые атрибуты (инициализируются после start_color). Смотри configure_curses.
int ATTR_OK = 0;
int ATTR_ERROR = 0;
int ATTR_ERROR_REVERSE = 0;
int ATTR_DIM = 0;

// ============================================================================
// Утилиты отображения
// ============================================================================

// Форматирование wide-строки (аналог snprintf для std::wstring)
std::wstring wsfmt(const wchar_t *fmt, ...) noexcept {
  wchar_t buf[192];
  va_list ap;
  va_start(ap, fmt);
  vswprintf(buf, sizeof(buf) / sizeof(buf[0]), fmt, ap);
  va_end(ap);
  return std::wstring(buf);
}

// Простая конверсия UTF-8 (малые символы, имена созвездий/фикса - ASCII)
std::wstring from_utf8(const char *s) noexcept {
  std::wstring out;
  if (!s)
    return out;
  while (*s) {
    const unsigned char c = static_cast<unsigned char>(*s++);
    if (c < 0x80) {
      out += static_cast<wchar_t>(c);
    } else if ((c & 0xE0) == 0xC0 && (*s & 0xC0) == 0x80) {
      out += static_cast<wchar_t>(((c & 0x1F) << 6) | (*s++ & 0x3F));
    } else if ((c & 0xF0) == 0xE0 && (s[0] & 0xC0) == 0x80 &&
               (s[1] & 0xC0) == 0x80) {
      out += static_cast<wchar_t>(((c & 0x0F) << 12) |
                                  ((s[0] & 0x3F) << 6) | (s[1] & 0x3F));
      s += 2;
    } else {
      out += L'?';
    }
  }
  return out;
}

// std::string (UTF-8) -> std::wstring
std::wstring wstr(const std::string &s) noexcept { return from_utf8(s.c_str()); }

// Форматирует double с фиксированной точностью и ТОЧКОЙ-разделителем,
// не завися от локали (printf/vswprintf в ru_RU пишет запятую, что ломает
// чтение координат). std::to_chars всегда использует десятичную точку.
std::wstring wfnum(double v, int prec) noexcept {
  char buf[64];
  auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), v,
                               std::chars_format::fixed, prec);
  (void)ec;
  return from_utf8(std::string(buf, p).c_str());
}

// ============================================================================
// DashboardStats - статистика и текущее состояние дашборда
// ============================================================================
struct DashboardStats {
  std::string port;        // имя последовательного порта
  uint32_t baudrate = 0;   // скорость обмена, бит/с
  std::wstring gnss_module_name; // имя GNSS-модуля (из SYS_MSG)

  uint64_t success = 0;
  uint64_t errors = 0;
  uint64_t reconnects = 0;
  uint32_t software_resets = 0;

  DataStreamType stream_format = DATA_STREAM_UNKNOWN; // тип потока (CSV/NMEA)
  uint32_t mfr_code = 0; // код производителя расширения NMEA-0183 или 0

  bool disconnected = true;
  bool is_stationary = false;
  double stationary_timer = 0.0;
  double last_data_time = 0.0;

  AccuracyTracker accuracy;
  LogWriter log;
};

// Сброс состояния стационарности и трекера точности
void reset_stationary_state(DashboardStats &stats) noexcept {
  stats.is_stationary = false;
  stats.stationary_timer = 0.0;
  stats.accuracy.reset();
}

// ============================================================================
// Базовый класс окна дашборда (ncursesw)
// ============================================================================
void wdraw(WINDOW *win, int y, int x, const std::wstring &s, int attr = 0,
           size_t max_chars = ~size_t{0}) noexcept {
  int my = 0, mx = 0;
  getmaxyx(win, my, mx);
  if (y < 0 || y >= my || x < 0 || x >= mx || s.empty())
    return;
  const size_t cap = static_cast<size_t>(mx - x);
  size_t n = s.size();
  if (max_chars < n)
    n = max_chars;
  if (n > cap)
    n = cap;
  if (n == 0)
    return;
  wattron(win, attr);
  mvwaddnwstr(win, y, x, s.c_str(), static_cast<int>(n));
  wattroff(win, attr);
}

class BaseWindow {
public:
  BaseWindow(WINDOW *win, int label_x, int value_x, const wchar_t *title)
      : win_(win), label_x_(label_x), value_x_(value_x), title_(title) {}

  virtual ~BaseWindow() {
    if (win_)
      delwin(win_);
  }

  BaseWindow(const BaseWindow &) = delete;
  BaseWindow &operator=(const BaseWindow &) = delete;

  void draw(const GNSSData &data, const DashboardStats &stats) noexcept {
    draw_box();
    cursor_y_ = CONTENT_START_Y;
    draw_content(data, stats);
  }

  void noutrefresh() noexcept { wnoutrefresh(win_); }

protected:
  virtual void draw_content(const GNSSData &data,
                            const DashboardStats &stats) noexcept = 0;

  WINDOW *win_ = nullptr;
  int label_x_ = LABEL_X_DEFAULT;
  int value_x_ = 14;
  int cursor_y_ = CONTENT_START_Y;
  std::wstring title_;

  void reset_cursor() noexcept { cursor_y_ = CONTENT_START_Y; }

  void draw_line(const std::wstring &text, int attr = 0) noexcept {
    wdraw(win_, cursor_y_, label_x_, text, attr);
    cursor_y_++;
  }

  void draw_labeled(const std::wstring &label, const std::wstring &value,
                    int value_attr = 0, int value_x = -1) noexcept {
    wdraw(win_, cursor_y_, label_x_, label, A_BOLD);
    wdraw(win_, cursor_y_, value_x >= 0 ? value_x : value_x_, value,
          value_attr);
    cursor_y_++;
  }

  void draw_conditional(bool condition, const std::wstring &true_text,
                        const std::wstring &false_text, int true_attr,
                        int false_attr) noexcept {
    if (condition)
      draw_line(true_text, true_attr);
    else
      draw_line(false_text, false_attr);
  }

  void draw_box() noexcept {
    werase(win_);
    int max_y = 0, max_x = 0;
    getmaxyx(win_, max_y, max_x);
    if (max_x < 4 || max_y < 3)
      return;

    // Рамка из Unicode-символов (box drawing)
    const std::wstring horiz(
        static_cast<size_t>(max_x - FRAME_MARGIN), L'\u2500');
    wdraw(win_, 0, 0, L"\u250C"); // top-left corner
    wdraw(win_, 0, 1, horiz);
    wdraw(win_, 0, max_x - 1, L"\u2510"); // top-right corner
    for (int y = 1; y < max_y - 1; ++y) {
      wdraw(win_, y, 0, L"\u2502");         // left border
      wdraw(win_, y, max_x - 1, L"\u2502"); // right border
    }
    wdraw(win_, max_y - 1, 0, L"\u2514"); // bottom-left corner
    wdraw(win_, max_y - 1, 1, horiz);
    wdraw(win_, max_y - 1, max_x - 1, L"\u2518"); // bottom-right corner

    // Заголовок окна
    const std::wstring t = L" " + title_ + L" ";
    wdraw(win_, 0, TITLE_OFFSET_X, t, A_BOLD);
  }
};

// ============================================================================
// PositionWindow - широта, долгота, высота, время/дата
// ============================================================================
class PositionWindow final : public BaseWindow {
public:
  PositionWindow(WINDOW *win) : BaseWindow(win, 2, 14, L"Positioning") {}

  void draw_content(const GNSSData &d, const DashboardStats &) noexcept override {
    const std::wstring lat = !std::isnan(d.latitude)
                                 ? wfnum(d.latitude, 6) + L"\u00B0"
                                 : PLACEHOLDER_W;
    const std::wstring lon = !std::isnan(d.longitude)
                                 ? wfnum(d.longitude, 6) + L"\u00B0"
                                 : PLACEHOLDER_W;
    draw_labeled(L"Latitude: ", lat);
    draw_labeled(L"Longitude:", lon);
    const std::wstring alt = !std::isnan(d.altitude)
                                 ? wfnum(d.altitude, 1) + L" m"
                                 : PLACEHOLDER_W + L" m";
    draw_labeled(L"Altitude: ", alt);
    draw_labeled(L"UTC Time: ",
                 d.time.empty() ? PLACEHOLDER_W : wstr(d.time));
    draw_labeled(L"UTC Date: ",
                 d.date.empty() ? PLACEHOLDER_W : wstr(d.date));
  }
};

// ============================================================================
// GNSSWindow - созвездие, спутники, HDOP, режим фикса
// ============================================================================
class GNSSWindow final : public BaseWindow {
public:
  GNSSWindow(WINDOW *win) : BaseWindow(win, 2, 18, L"GNSS Parameters") {}

  void draw_content(const GNSSData &d, const DashboardStats &) noexcept override {
    draw_labeled(L"Constellation:",
                 d.constellation.empty() ? PLACEHOLDER_W : wstr(d.constellation));
    draw_labeled(L"Satellites:   ",
                 d.satellites >= 0 ? wsfmt(L"%d", d.satellites) : PLACEHOLDER_W);
    draw_labeled(L"HDOP:         ",
                 !std::isnan(d.hdop) ? wfnum(d.hdop, 1) : PLACEHOLDER_W);
    const std::wstring fix =
        d.fix_mode.empty() ? PLACEHOLDER_W : wstr(d.fix_mode);
    // Невалидный фикс - подсветка реверсом
    const int attr = (d.fix_mode == "Not Valid") ? ATTR_ERROR_REVERSE : 0;
    draw_labeled(L"Fix Mode:     ", fix, attr);
  }
};

// ============================================================================
// MotionWindow - динамика движения / анализ точности (при стоянке)
// ============================================================================
class MotionWindow final : public BaseWindow {
public:
  MotionWindow(WINDOW *win) : BaseWindow(win, 2, 20, L"Motion Dynamics") {}

  void draw_content(const GNSSData &d, const DashboardStats &stats) noexcept override {
    if (stats.is_stationary)
      draw_accuracy(stats.accuracy);
    else
      draw_motion(d, stats);
  }

private:
  void draw_motion(const GNSSData &d, const DashboardStats &stats) noexcept {
    wdraw(win_, 0, TITLE_OFFSET_X, L" Motion Dynamics ", A_BOLD);

    draw_labeled(L"Speed:", wstr(format_speed(d.speed_kmh)));

    // Курс доступен, если есть значение; иначе объясняем причину отсутствия
    std::wstring course_s;
    const double speed_kmh = !std::isnan(d.speed_kmh) ? d.speed_kmh : 0.0;
    if (!std::isnan(d.course)) {
      course_s = wfnum(d.course, 1) + L"\u00B0 (" +
                 wstr(compass_letter(d.course)) + L")";
    } else if (std::isnan(d.speed_kmh) || speed_kmh < STATIONARY_SPEED_KMH) {
      course_s = PLACEHOLDER_W + L" (too slow)";
    } else if (d.fix_mode == "Not Valid") {
      course_s = PLACEHOLDER_W + L" (no fix)";
    } else {
      course_s = PLACEHOLDER_W;
    }
    draw_labeled(L"Course:", course_s);

    if (stats.stationary_timer > 0.0) {
      const double remaining =
          STATIONARY_TIME_S - (now() - stats.stationary_timer);
      if (remaining > 0.0 && remaining < STATIONARY_TIME_S) {
        draw_line(wsfmt(L"Accuracy mode in: %ds", static_cast<int>(remaining)),
                  ATTR_DIM);
      }
    }
  }

  void draw_accuracy(const AccuracyTracker &tracker) noexcept {
    wdraw(win_, 0, TITLE_OFFSET_X, L" Accuracy Analysis ", ATTR_OK);

    AccuracyMetrics m;
    if (!tracker.get_metrics(m)) {
      draw_line(wsfmt(L"Collecting data: %u/%u points", tracker.n(),
                      MIN_POINTS_FOR_ACCURACY));
      return;
    }

    draw_labeled(L"Points:       ", wsfmt(L"%u", m.n));
    draw_labeled(L"Valid Fix:    ", wfnum(m.valid_pct, 1) + L"%");
    draw_labeled(L"Avg HDOP:     ", wfnum(m.avg_hdop, 2));
    draw_line(std::wstring(DIVIDER_LENGTH, L'-'), ATTR_DIM);
    draw_labeled(L"2D Error(1\u03C3): ", L"+/-" + wfnum(m.err_2d_m, 2) + L" m",
                 ATTR_ERROR);
    draw_labeled(L"Drift:        ", L"~" + wfnum(m.drift_m, 2) + L" m");
  }
};

// ============================================================================
// StatusWindow - статус соединения, статистика, логирование
// ============================================================================
class StatusWindow final : public BaseWindow {
public:
  StatusWindow(WINDOW *win) : BaseWindow(win, 2, 2, L"Connection Status") {}

  void draw_content(const GNSSData &, const DashboardStats &stats) noexcept override {
    const std::wstring port =
        wstr(stats.port) + L" (" + wstr(get_port_type(stats.port)) + L")";
    draw_line(L"Port: " + port);
    draw_line(L"Module: " + stats.gnss_module_name);
    draw_line(wsfmt(L"Speed: %u (USB max)", stats.baudrate));

    if (stats.disconnected) {
      draw_line(L"Status: DISCONNECTED (reconnecting...)", ATTR_ERROR);
    } else {
      // Тип потока данных, определённый detect_format() для последней строки,
      // + код производителя расширения NMEA-0183 ($P...) при его наличии
      // (как _build_conn_str в gnss_dashboard.py)
      std::wstring status =
          L"Status: CONNECTED (" +
          wstr(stream_format_by_id(stats.stream_format)) + L")";
      if (stats.mfr_code != 0)
        status += L" |" + wstr(code_to_mfr_string(stats.mfr_code));
      draw_line(status, ATTR_OK);
    }

    draw_line(wsfmt(L"Valid lines: %llu",
                    static_cast<unsigned long long>(stats.success)));
    draw_line(wsfmt(L"Errors: %llu",
                    static_cast<unsigned long long>(stats.errors)));
    draw_line(wsfmt(L"Reconnects: %llu",
                    static_cast<unsigned long long>(stats.reconnects)));
    draw_line(wsfmt(L"SW Resets: %u", stats.software_resets));
    draw_line(std::wstring(DIVIDER_LENGTH, L'-'), ATTR_DIM);

    if (stats.log.is_writing() && stats.last_data_time > 0) {
      // Проверяем реальную активность данных
      const bool active =
          (now() - stats.last_data_time) < LOG_ACTIVITY_TIMEOUT_S;
      draw_conditional(active, L"Logging: ACTIVE",
                       L"Logging: INACTIVE (no data)", ATTR_OK, ATTR_DIM);
    } else {
      draw_line(L"Logging: INACTIVE", ATTR_DIM);
    }
    draw_line(wsfmt(L"Lines written: %llu",
                    static_cast<unsigned long long>(
                        stats.log.lines_written())));
  }
};

// ============================================================================
// Dashboard - координатор: парсер -> данные -> окна
// ============================================================================
class Dashboard {
public:
  Dashboard(WINDOW *stdscr, SerialParser &parser)
      : stdscr_(stdscr), parser_(parser) {
    stats_.port = parser.port();
    stats_.baudrate = parser.baudrate();
    stats_.gnss_module_name = wstr(DEFAULT_MODULE_NAME);
    stats_.disconnected = !parser.is_open();

    configure_curses();
    build_layout();
  }

  ~Dashboard() { panels_.clear(); }

  Dashboard(const Dashboard &) = delete;
  Dashboard &operator=(const Dashboard &) = delete;

  void run() noexcept {
    while (true) {
      if (handle_input())
        break;
      if (stats_.disconnected)
        try_reconnect();
      else
        poll_serial();
      render();
    }
  }

private:
  void configure_curses() noexcept {
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    use_default_colors();
    init_pair(COLOR_ERROR, COLOR_RED, -1);
    init_pair(COLOR_OK, COLOR_GREEN, -1);
    keypad(stdscr_, TRUE);
    nodelay(stdscr_, TRUE);
    timeout(UI_TIMEOUT_MS);

    // Инициализация цветовых атрибутов ПОСЛЕ start_color()
    ATTR_OK = A_BOLD | COLOR_PAIR(COLOR_OK);
    ATTR_ERROR = A_BOLD | COLOR_PAIR(COLOR_ERROR);
    ATTR_ERROR_REVERSE = A_REVERSE | COLOR_PAIR(COLOR_ERROR);
    ATTR_DIM = A_DIM;
  }

  void build_layout() noexcept {
    panels_.clear();
    int h = 0, w = 0;
    getmaxyx(stdscr_, h, w);
    if (h < MIN_TERM_HEIGHT || w < MIN_TERM_WIDTH)
      return;

    const int mid_h = h / 2;
    const int mid_w = w / 2;

    // 2x2 разметка как в python-версии
    if (WINDOW *win = newwin(mid_h, mid_w, 0, 0))
      panels_.emplace_back(new PositionWindow(win));
    if (WINDOW *win = newwin(mid_h, w - mid_w, 0, mid_w))
      panels_.emplace_back(new GNSSWindow(win));
    if (WINDOW *win = newwin(h - mid_h, mid_w, mid_h, 0))
      panels_.emplace_back(new MotionWindow(win));
    if (WINDOW *win = newwin(h - mid_h, w - mid_w, mid_h, mid_w))
      panels_.emplace_back(new StatusWindow(win));
  }

  // Обрабатывает нажатия клавиш. Возвращает true, если запрошен выход.
  bool handle_input() noexcept {
    const int key = getch();
    if (key == 'q' || key == 'Q' || key == VK_ESCAPE)
      return true;
#ifdef KEY_RESIZE
    if (key == KEY_RESIZE)
      build_layout();
#endif
    return false;
  }

  // Обрабатывает системные сообщения от микроконтроллера (MCU).
  // Формат: "SYS_MSG:<тип>:<данные>".
  void handle_sys_message(const std::string &raw_line) noexcept {
    const size_t start = raw_line.find(SYS_MSG_PREFIX);
    if (start == std::string::npos)
      return;

    const std::string msg = raw_line.substr(start);
    const size_t c1 = msg.find(':');
    const size_t c2 = (c1 == std::string::npos) ? std::string::npos
                                                : msg.find(':', c1 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos)
      return;

    const int msg_type = atoi(msg.c_str() + c1 + 1);

    // Простая очистка: берём первое "слово" (отсекает \r\n и мусор)
    const std::string rest = msg.substr(c2 + 1);
    size_t pb = 0;
    while (pb < rest.size() && (rest[pb] == ' ' || rest[pb] == '\t'))
      ++pb;
    size_t pe = pb;
    while (pe < rest.size() && rest[pe] != ' ' && rest[pe] != '\t')
      ++pe;
    const std::string payload = rest.substr(pb, pe - pb);

    if (msg_type == MCU_MSG_MODULE_DETECTED) {
      stats_.gnss_module_name = wstr(payload);
    } else if (msg_type == MCU_MSG_SOFTWARE_RESET) {
      log_msg("MCU: Software reset initiated (" + payload + ")", stderr);
      stats_.success = 0;
      stats_.errors = 0;
      // Инкремент счётчика программных сбросов
      stats_.software_resets++;
      stats_.log.reset_count();
    } else if (msg_type == MCU_MSG_WATCHDOG_TRIGGERED) {
      log_msg("MCU: Watchdog triggered (" + payload + ")", stderr);
    }
  }

  // Пытается переподключиться к порту с учётом задержки между попытками.
  void try_reconnect() noexcept {
    const double t = now();
    if (t - last_reconnect_attempt_ < RECONNECT_DELAY_S)
      return;
    last_reconnect_attempt_ = t;

    if (parser_.reconnect()) {
      stats_.disconnected = false;
      stats_.reconnects++;
      stats_.success = 0;
      stats_.errors = 0;
      stats_.log.reset_count();
    }
  }

  // Опрашивает последовательный порт и обновляет данные и статистику.
  void poll_serial() noexcept {
    const SerialParser::Result res = parser_.poll();
    stats_.stream_format = parser_.stream_format();
    stats_.mfr_code = parser_.mfr_code();

    if (!res.raw_line.empty()) {
      // Системные сообщения или логирование строк (CSV-формат)
      if (res.is_sys_msg)
        handle_sys_message(res.raw_line);
      else
        stats_.log.write(res.raw_line, res.has_data ? &res.data : nullptr);
    }

    if (res.has_data) {
      data_ = res.data;
      stats_.success++;
      stats_.last_data_time = now();

      const double speed_kmh =
          std::isnan(data_.speed_kmh) ? 0.0 : data_.speed_kmh;
      const double t = now();

      if (speed_kmh < STATIONARY_SPEED_KMH) {
        if (!stats_.is_stationary) {
          if (stats_.stationary_timer == 0.0) {
            stats_.stationary_timer = t;
          } else if (t - stats_.stationary_timer >= STATIONARY_TIME_S) {
            stats_.is_stationary = true;
          }
        }
        // Точки собираем всё время "покоя" - и во время таймера (33 с), и в
        // стационарном режиме, без сброса при переходе. Благодаря этому к
        // моменту входа в режим точности выборка уже накоплена, и 2D Error /
        // Drift показывают реальные значения сразу, а не нули первых секунд.
        stats_.accuracy.add_point(data_);
      } else {
        // Движение - сбрасываем стационарность и трекер точности
        reset_stationary_state(stats_);
      }
    } else if (res.is_error) {
      stats_.errors++;
    }
  }

  void render() noexcept {
    int h = 0, w = 0;
    getmaxyx(stdscr_, h, w);
    if (h < MIN_TERM_HEIGHT || w < MIN_TERM_WIDTH) {
      // Терминал слишком мал - предупреждение по центру
      werase(stdscr_);
      const std::wstring msg = wsfmt(
          L"Terminal too small! Minimum: %dx%d. Press q, Q, ESCAPE to exit! "
          L"Errors see in 'mcu_debug.log' file.",
          MIN_TERM_WIDTH, MIN_TERM_HEIGHT);
      const int x = std::max(0, (w - static_cast<int>(msg.size())) / 2);
      wdraw(stdscr_, h / 2, x, msg, A_BOLD | A_REVERSE);
      wnoutrefresh(stdscr_);
      doupdate();
      return;
    }

    for (auto &panel : panels_) {
      panel->draw(data_, stats_);
      panel->noutrefresh();
    }
    doupdate();
  }

  WINDOW *stdscr_ = nullptr;
  SerialParser &parser_;
  DashboardStats stats_;
  GNSSData data_;
  std::vector<std::unique_ptr<BaseWindow>> panels_;
  double last_reconnect_attempt_ = 0.0;
};

// ============================================================================
// RAII-обёртка curses
// ============================================================================
class CursesGuard {
public:
  CursesGuard() noexcept { ok_ = (initscr() != nullptr); }
  ~CursesGuard() noexcept {
    if (ok_)
      endwin();
  }
  CursesGuard(const CursesGuard &) = delete;
  CursesGuard &operator=(const CursesGuard &) = delete;
  [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
  bool ok_ = false;
};

// Проверка/установка переменной TERM (как ensure_terminal() в python)
void ensure_term() noexcept {
  const char *term = getenv("TERM");
  if (!term || !*term || strcmp(term, "unknown") == 0)
    setenv("TERM", "xterm-256color", 1);
}

} // namespace

// ============================================================================
// Main
// ============================================================================
int main(int argc, char **argv) {
  // stderr -> mcu_debug.log (не мешает TUI); без буферизации, чтобы журнал
  // обновлялся немедленно (при завершении процесса буфер не теряется)
  freopen("mcu_debug.log", "a", stderr);
  setvbuf(stderr, nullptr, _IONBF, 0);

  std::string port;
  uint32_t baudrate = DEFAULT_BAUDRATE;
  if (!parse_args(argc, argv, port, baudrate))
    return 1;

  // Автодетект USB-CDC, если порт не задан явно
  if (port.empty()) {
    if (!detect_port(port)) {
      fprintf(stderr,
              "Error: no USB-UART ports (ttyACM* / ttyUSB*) were found.\n"
              "Connect the board or specify the port:\n"
              "  gnss_dashboard /dev/ttyACM0\n"
              "  gnss_dashboard /dev/ttyUSB0 9600\n");
      // stderr перенаправлен в mcu_debug.log - продублируем на консоль
      fprintf(stdout,
              "gnss_dashboard: ERROR - no USB-UART ports found, exiting.\n"
              "  Details and all diagnostics: see 'mcu_debug.log'.\n"
              "  Connect the board or specify the port explicitly:\n"
              "    gnss_dashboard /dev/ttyACM0\n"
              "    gnss_dashboard /dev/ttyUSB0 9600\n");
      return 1;
    }
    fprintf(stderr, "[gnss_dashboard] auto-detected port: %s\n", port.c_str());
  }

  ensure_term();
  setlocale(LC_ALL, "");

  CursesGuard guard;
  if (!guard.ok()) {
    fprintf(stderr, "gnss_dashboard: curses init failed\n");
    fprintf(stdout,
            "gnss_dashboard: ERROR - curses init failed. "
            "See 'mcu_debug.log' for details.\n");
    return 1;
  }

  SerialParser parser;
  if (!parser.open(port, baudrate)) {
    fprintf(stderr, "[gnss_dashboard] cannot open %s @ %u\n", port.c_str(),
            static_cast<unsigned>(baudrate));
    fprintf(stdout, "[gnss_dashboard] ERROR - cannot open %s @ %u. "
                    "See 'mcu_debug.log' for details.\n",
            port.c_str(), static_cast<unsigned>(baudrate));
    return 1;
  }
  fprintf(stderr, "[gnss_dashboard] port opened: %s @ %u\n", port.c_str(),
          static_cast<unsigned>(baudrate));

  Dashboard dash(stdscr, parser);
  dash.run();
  parser.close();
  return 0;
}