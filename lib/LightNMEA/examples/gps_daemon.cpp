/*
 * gps_daemon.cpp - LightNMEA GPS daemon (POSIX / Linux)
 *
 * Читает NMEA-0183 с последовательного порта и пишет CSV-строки с валидной
 * позицией в файл или на stdout. Умеет уходить в фон (демонизация),
 * корректно завершается по SIGINT/SIGTERM, периодически печатает статистику
 * и переживает отключение устройства (порт переоткрывается автоматически).
 *
 * Пайплайн:
 *   SerialStream (UART) -> NMEAStreamReader -> Parser -> to_csv()
 *
 * Сборка (из директории examples/):
 *   g++ -std=c++17 -O2 -I../include gps_daemon.cpp -o gps_daemon
 *   (или вызовом CMake: см. lib/LightNMEA/CMakeLists.txt, цель gps_daemon)
 *
 * Запуск:
 *   ./gps_daemon /dev/ttyACM0 9600 /tmp/gps.csv          # foreground
 *   ./gps_daemon -d /dev/ttyUSB0 38400 /var/log/gps.csv  # демон в фоне
 *   ./gps_daemon -s 0 /dev/ttyACM0                       # без статистики
 *   ./gps_daemon -h                                      # справка
 */

#include "nmea_formatter.hpp"
#include "nmea_stream_reader.hpp"
#include "serial_stream.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace light_nmea;

// ============================================================================
// Глобальное состояние и сигналы
// ============================================================================

static volatile sig_atomic_t g_running = 1;
static void on_signal(int) { g_running = 0; }

// Монотонное время в мс (для анти-спам фильтра NMEAStreamReader)
uint32_t get_time_ms() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ============================================================================
// Демонизация (классический двойной fork)
// ============================================================================

bool daemonize() {
  pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid > 0)
    _exit(0); // родительский процесс завершается сразу
  if (setsid() < 0)
    return false;
  pid = fork();
  if (pid < 0)
    return false;
  if (pid > 0)
    _exit(0);

  umask(0);
  if (chdir("/") != 0)
    return false;

  // stdio -> /dev/null (данные демон пишет в файл)
  int devnull = open("/dev/null", O_RDWR);
  if (devnull < 0)
    return false;
  dup2(devnull, STDIN_FILENO);
  dup2(devnull, STDOUT_FILENO);
  dup2(devnull, STDERR_FILENO);
  if (devnull > STDERR_FILENO)
    close(devnull);
  return true;
}

// ============================================================================
// Справка
// ============================================================================

void usage(const char *prog) {
  std::fprintf(stderr,
               "LightNMEA GPS daemon\n"
               "Usage: %s [options] <device> [baud] [output]\n"
               "  <device>  serial port, e.g. /dev/ttyACM0, /dev/ttyUSB0\n"
               "  baud      baud rate (default 9600)\n"
               "  output    CSV file (default: stdout, '-' = stdout)\n"
               "Options:\n"
               "  -d        daemonize (background)\n"
               "  -s <sec>  stats interval, 0 = off (default 60)\n"
               "  -h        this help\n",
               prog);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
  bool background = false;
  long stats_interval_s = 60;

  int opt;
  while ((opt = getopt(argc, argv, "ds:h")) != -1) {
    switch (opt) {
    case 'd':
      background = true;
      break;
    case 's':
      stats_interval_s = std::atol(optarg);
      break;
    case 'h':
    default:
      usage(argv[0]);
      return (opt == 'h') ? 0 : 1;
    }
  }

  const char *device = (optind < argc) ? argv[optind++] : nullptr;
  int baud = (optind < argc) ? std::atoi(argv[optind++]) : 9600;
  const char *output = (optind < argc) ? argv[optind++] : nullptr;
  if (!device) {
    usage(argv[0]);
    return 1;
  }

  if (background) {
    if (!daemonize()) {
      std::perror("daemonize");
      return 1;
    }
    if (!output)
      output = "/tmp/gps_daemon.csv"; // в фоне обязателен файл
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  // Выходной поток: файл или stdout
  FILE *out = stdout;
  bool close_out = false;
  if (output && std::strcmp(output, "-") != 0) {
    out = std::fopen(output, "a");
    if (!out) {
      std::perror(output);
      return 1;
    }
    close_out = true;
  }

  SerialStream uart;
  NMEAStreamReader reader;
  reader.set_anti_spam_interval(0); // фильтр выключен: нужны все пакеты
  Parser parser;

  uint32_t last_time = 0;
  unsigned long long emit_count = 0;
  time_t last_stats = 0;

  std::fprintf(out, "# light-nmea-cpp daemon: lines with a valid fix\n");
  std::fflush(out);

  while (g_running) {
    // Порт может быть временно недоступен (устройство переподключено) -
    // переоткрывает с паузой.
    if (!uart.is_open()) {
      if (uart.open(device, static_cast<uint32_t>(baud))) {
        std::fprintf(stderr, "[gps_daemon] opened %s @ %d baud\n", device, baud);
      } else {
        timespec ts{};
        ts.tv_sec = 5; // ждём 5 секунд перед повторной попыткой
        nanosleep(&ts, nullptr);
        continue;
      }
    }

    // Читает доступные байты, собирает пакеты и передаёт парсеру.
    reader.read_available(parser, uart, get_time_ms);

    // Одна CSV-строка на новое валидное положение.
    const auto &data = parser.data();
    if (data.has_valid_position() && data.time_utc != 0 &&
        data.time_utc != last_time) {
      last_time = data.time_utc;

      char line[256];
      to_csv(data, line, sizeof(line), SpeedUnit::KMH, DistanceUnit::METERS);
      std::fprintf(out, "%s\n", line);
      std::fflush(out);
      ++emit_count;
    }

    // Периодическая статистика в stderr.
    if (stats_interval_s > 0) {
      const time_t now = std::time(nullptr);
      if (now != last_stats) {
        last_stats = now;
        const auto stats = reader.get_stats();
        std::fprintf(stderr,
                     "[gps_daemon] processed=%zu aborted=%zu spam=%zu "
                     "emitted=%llu\n",
                     stats.packets_processed, stats.packets_aborted,
                     stats.anti_spam_dropped, emit_count);
      }
    }

    // Пауза 10 мс, чтобы не грузить CPU при отсутствии данных.
    timespec ts{};
    ts.tv_nsec = 10 * 1000000;
    nanosleep(&ts, nullptr);
  }

  const auto stats = reader.get_stats();
  std::fprintf(stderr,
               "[gps_daemon] exit: processed=%zu aborted=%zu spam=%zu "
               "emitted=%llu\n",
               stats.packets_processed, stats.packets_aborted,
               stats.anti_spam_dropped, emit_count);

  uart.close();
  if (close_out)
    std::fclose(out);
  return 0;
}