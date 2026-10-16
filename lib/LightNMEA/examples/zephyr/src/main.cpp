/*
 * LightNMEA Zephyr example
 *
 * Читает NMEA-данные из UART вызовом API uart_pipe, накапливает байты в
 * кольцевом буфере и парсит их в главном цикле библиотекой LightNMEA (NMEAStreamReader +
 * Parser).
 *
 * Ядро LightNMEA (light_nmea.hpp, nmea_stream_reader.hpp, nmea_formatter.hpp)
 * не зависит от Arduino/POSIX, поэтому работает в Zephyr без изменений.
 *
 * Сборка: см. README.md.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart_pipe.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "nmea_formatter.hpp"
#include "nmea_stream_reader.hpp"

using namespace light_nmea;

// ============================================================================
// Кольцевой буфер для байт из UART (заполняется в обработчике uart_pipe)
// ============================================================================
#define RING_SIZE 512
static uint8_t ring_data[RING_SIZE];
static volatile uint16_t ring_head = 0;
static volatile uint16_t ring_tail = 0;

// ============================================================================
// Stream поверх кольцевого буфера
// ============================================================================
// NMEAStreamReader читает байты вызовом Stream::read().
class RingStream final : public Stream {
public:
  size_t read(uint8_t *buffer, size_t max_len) override {
    size_t n = 0;
    while (n < max_len && ring_head != ring_tail) {
      buffer[n++] = ring_data[ring_tail];
      ring_tail = static_cast<uint16_t>((ring_tail + 1) % RING_SIZE);
    }
    return n;
  }
};

// ============================================================================
// Обработчик uart_pipe: байты из UART -> кольцевой буфер
// ============================================================================
// Новый API uart_pipe: драйвер пишет принятые байты в буфер
// и вызывает callback(buf, &off); callback возвращает буфер для следующего
// приёма (обычно тот же) и сбрасывает off в 0.
static uint8_t pipe_buffer[64];

static uint8_t *pipe_handler(uint8_t *buf, size_t *off) {
  for (size_t i = 0; i < *off; ++i) {
    const uint16_t next = static_cast<uint16_t>((ring_head + 1) % RING_SIZE);
    if (next != ring_tail) { // буфер не переполнен
      ring_data[ring_head] = buf[i];
      ring_head = next;
    }
  }
  *off = 0;
  return buf;
}

// ============================================================================
// Время в миллисекундах (для анти-спам фильтра NMEAStreamReader)
// ============================================================================
static uint32_t get_time_ms() { return k_uptime_get_32(); }

// ============================================================================
// main
// ============================================================================
int main(void) {
  Parser parser;
  NMEAStreamReader reader;
  RingStream stream;

  uart_pipe_register(pipe_buffer, sizeof(pipe_buffer), pipe_handler);
  printk("LightNMEA Zephyr example started\n");

  while (1) {
    const size_t processed = reader.read_available(parser, stream, get_time_ms);
    if (processed > 0) {
      const NMEAData &data = parser.data();
      if (data.has_coordinates()) {
        char buf[256];
        to_txt(data, buf, sizeof(buf));
        printk("%s\n", buf);
      }
    }
    k_sleep(K_MSEC(10));
  }

  return 0;
}
