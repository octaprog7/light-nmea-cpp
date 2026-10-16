#include "nmea_stream_reader.hpp"
#include <cstring>
#include <iostream>

using namespace light_nmea;

// ============================================================================
// Пример 1: наследник от Stream (например, обёртка над UART).
// ============================================================================
// Любой источник байт (UART, кольцевой буфер, очередь RTOS и т.д.)
// реализует абстрактный метод read() базового класса Stream.
struct MockUARTStream final : public Stream {
  const char *data =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*4F\r\n";
  size_t offset = 0;

  size_t read(uint8_t *buf, size_t max_len) override {
    size_t remaining = std::strlen(data) - offset;
    if (remaining == 0)
      return 0;
    size_t to_copy = (remaining < max_len) ? remaining : max_len;
    std::memcpy(buf, data + offset, to_copy);
    offset += to_copy;
    return to_copy;
  }
};

// Функция получения текущего времени в мс (для анти-спам фильтра).
uint32_t get_time_ms() { return 1000; }

// ============================================================================
// Пример 2: чтение из файла реализацией FileStream.
// ============================================================================
void demo_file_stream(const char *path) {
  Parser parser;
  NMEAStreamReader reader;

  FileStream file;
  if (!file.open(path)) {
    std::cout << "Failed to open file: " << path << "\n";
    return;
  }

  reader.read_available(parser, file, get_time_ms);

  auto stats = reader.get_stats();
  std::cout << "File '" << path << "' -> packets processed: "
            << stats.packets_processed << "\n";
}

// Callback для статистики
void on_packet(bool recognized, bool valid, Constellation cst) {
  if (recognized && valid) {
    std::cout << "Valid packet, constellation: " << static_cast<int>(cst) << "\n";
  }
}

int main() {
  Parser parser;
  MockUARTStream uart;

  NMEAStreamReader reader;
  reader.set_anti_spam_interval(0); // 0 = фильтр отключён

  reader.read_available(parser, uart, get_time_ms, on_packet);

  auto stats = reader.get_stats();
  std::cout << "Processed: " << stats.packets_processed
            << ", aborted: " << stats.packets_aborted << "\n";

  // Чтение из файла (потомок Stream).
  // Путь к любому *.nmea-треку, например:
  //   ../data/nmea/trace_000.nmea   (при запуске из build/examples)
  demo_file_stream("trace_000.nmea");

  return 0;
}