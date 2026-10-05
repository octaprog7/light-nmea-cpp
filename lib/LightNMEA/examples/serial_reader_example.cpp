/*
 * LightNMEA - Ultra-fast NMEA GPS parser
 *
 * Пример чтения NMEA-данных напрямую с последовательного порта (/dev/ttyXXX)
 * на POSIX. Показывает связку:
 *
 *   SerialStream (UART) -> NMEAStreamReader -> Parser
 *
 * Запуск:
 *   ./serial_reader_example /dev/ttyUSB0 9600
 *
 * Для выхода нажмите Ctrl-C.
 */

#include "nmea_stream_reader.hpp"
#include "serial_stream.hpp"

#include <cstdint>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iostream>

using namespace light_nmea;

// Флаг выхода по Ctrl-C
static volatile sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }

// Текущее время в мс (для анти-спам фильтра)
uint32_t get_time_ms() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Callback для каждого готового пакета
void on_packet(bool recognized, bool valid, Constellation cst) {
  // Печатаем только валидные пакеты с координатами.
  // (Здесь можно собирать статистику/отправлять по сети и т.д.)
  if (!recognized || !valid)
    return;
  std::cout << "Packet ok, constellation=" << static_cast<int>(cst) << "\n";
}

int main(int argc, char **argv) {
  const char *device = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
  const uint32_t baud = (argc > 2) ? static_cast<uint32_t>(atoi(argv[2])) : 9600;

  std::signal(SIGINT, on_signal);

  SerialStream uart;
  if (!uart.open(device, baud)) {
    std::cerr << "Failed to open " << device << "\n";
    return 1;
  }
  std::cout << "Opened port " << device << " @ " << baud << " baud\n";

  Parser parser;
  NMEAStreamReader reader;
  reader.set_anti_spam_interval(0); // 0 = фильтр выключен

  while (g_running && uart.is_open()) {
    // Читаем доступные байты, формируем пакеты и сразу передаём парсеру.
    reader.read_available(parser, uart, get_time_ms, on_packet);

    // После обработки можно получить доступ к распарсенным данным:
    // const auto& data = parser.data();
    // if (data.has_valid_position()) { ... }
  }

  auto stats = reader.get_stats();
  std::cout << "Processed: " << stats.packets_processed
            << ", aborted: " << stats.packets_aborted << "\n";
  return 0;
}