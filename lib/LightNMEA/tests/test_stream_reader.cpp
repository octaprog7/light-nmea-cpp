#include "nmea_stream_reader.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace light_nmea;

// Mock UART, который возвращает все данные сразу (с использованием offset).
// Наследует абстрактный Stream, как и любой другой источник NMEA-данных.
struct MockUART : public Stream {
  const char *all_data =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*4F\r\n"
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*"
      "6A\r\n";
  size_t offset = 0;

  size_t read(uint8_t *buf, size_t max_len) override {
    size_t remaining = strlen(all_data) - offset;
    if (remaining == 0)
      return 0;
    size_t to_copy = (remaining < max_len) ? remaining : max_len;
    memcpy(buf, all_data + offset, to_copy);
    offset += to_copy;
    return to_copy;
  }
};

// Mock время
uint32_t g_mock_time_ms = 0;
uint32_t get_time_ms() { return g_mock_time_ms; }

// Счетчик вызовов callback
int g_callback_count = 0;
Constellation g_last_constellation = Constellation::Unknown;

void on_packet(bool recognized, bool valid, Constellation cst) {
  (void)recognized; // параметр сохранён для полноты API колбэка
  (void)valid;
  g_callback_count++;
  g_last_constellation = cst;
}

void test_basic_stream_reading() {
  Parser parser;
  MockUART uart;

  NMEAStreamReader reader;
  g_callback_count = 0;

  // Читаем пакеты
  size_t processed =
      reader.read_available(parser, uart, get_time_ms, on_packet);

  assert(processed == 3);
  assert(g_callback_count == 3);

  auto stats = reader.get_stats();
  assert(stats.packets_processed == 3);
  assert(stats.packets_aborted == 0);

  std::cout << " test_basic_stream_reading passed\n";
}

void test_anti_spam_filter() {
  Parser parser;
  MockUART uart;

  NMEAStreamReader reader;
  reader.set_anti_spam_interval(1000); // 1 секунда
  g_callback_count = 0;
  g_mock_time_ms = 10000; // Начинаем с 10 секунд

  // Первый вызов - проходят только первые пакеты каждого типа (RMC + GGA)
  // Третий RMC блокируется, так как пришел в то же время
  reader.read_available(parser, uart, get_time_ms, on_packet);
  assert(g_callback_count == 2);

  auto stats = reader.get_stats();
  assert(stats.anti_spam_dropped == 1);

  std::cout << " test_anti_spam_filter passed\n";
}

void test_anti_spam_after_interval() {
  Parser parser;
  MockUART uart;

  NMEAStreamReader reader;
  reader.set_anti_spam_interval(1000);
  g_callback_count = 0;
  g_mock_time_ms = 10000;

  // Первый вызов
  reader.read_available(parser, uart, get_time_ms, on_packet);
  assert(g_callback_count == 2);

  // Прошло 1.5 секунды - фильтр должен пропустить новые пакеты
  g_mock_time_ms = 11500;
  uart.offset = 0; // Сбрасываем UART
  g_callback_count = 0;
  reader.read_available(parser, uart, get_time_ms, on_packet);

  // Снова проходят только RMC и GGA (третий RMC блокируется внутри того же
  // буфера)
  assert(g_callback_count == 2);

  std::cout << " test_anti_spam_after_interval passed\n";
}

void test_aborted_packets() {
  // UART, который возвращает битый пакет (без конца \r\n)
  struct BrokenUART : public Stream {
    const char *data = "$GPRMC,123519,A,BROKEN";
    size_t offset = 0;

    size_t read(uint8_t *buf, size_t max_len) override {
      // Отдаём данные один раз, затем конец потока (0), как реальный
      // неблокирующий источник.
      size_t len = strlen(data);
      if (offset >= len)
        return 0;
      if (len > max_len)
        len = max_len;
      memcpy(buf, data + offset, len);
      offset += len;
      return len;
    }
  };

  Parser parser;
  BrokenUART uart;

  NMEAStreamReader reader;
  reader.read_available(parser, uart, get_time_ms, nullptr);

  // Пакет не обработан, т.к. не было конца строки
  auto stats = reader.get_stats();
  assert(stats.packets_processed == 0);

  std::cout << " test_aborted_packets passed\n";
}

void test_reset() {
  Parser parser;
  MockUART uart;

  NMEAStreamReader reader;
  reader.read_available(parser, uart, get_time_ms, nullptr);

  auto stats1 = reader.get_stats();
  assert(stats1.packets_processed == 3);

  reader.reset();

  auto stats2 = reader.get_stats();
  assert(stats2.packets_processed == 0);
  assert(stats2.packets_aborted == 0);
  assert(stats2.anti_spam_dropped == 0);

  std::cout << " test_reset passed\n";
}

int main() {
  std::cout << "Running NMEAStreamReader tests...\n\n";

  test_basic_stream_reading();
  test_anti_spam_filter();
  test_anti_spam_after_interval();
  test_aborted_packets();
  test_reset();

  std::cout << "\n All NMEAStreamReader tests passed!\n";
  return 0;
}