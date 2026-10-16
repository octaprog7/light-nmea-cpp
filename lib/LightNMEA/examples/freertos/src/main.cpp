/*
 * LightNMEA FreeRTOS example (host, POSIX port)
 *
 * Реализует интеграцию LightNMEA с FreeRTOS:
 *   - задача-производитель читает NMEA-трек из файла data/sample.nmea
 *     и отправляет байты в очередь FreeRTOS;
 *   - задача-потребитель реализует абстрактный Stream поверх очереди
 *     и парсит данные путём NMEAStreamReader + Parser.
 *
 * Ядро LightNMEA (light_nmea.hpp, nmea_stream_reader.hpp, nmea_formatter.hpp)
 * не зависит от Arduino/POSIX, поэтому работает в любой задаче FreeRTOS.
 *
 * Сборка: см. CMakeLists.txt. Запуск из каталога examples/freertos:
 *   ./build/freertos_nmea_example
 */

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "nmea_formatter.hpp"
#include "nmea_stream_reader.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace light_nmea;

// ============================================================================
// Байтовая очередь для связи двух задач по данным
// ============================================================================
static QueueHandle_t nmea_queue = nullptr;

// ============================================================================
// Stream поверх очереди FreeRTOS
// ============================================================================
// NMEAStreamReader читает байты абстрактным Stream::read().
// Метод read() забирает доступные байты из очереди (неблокирующий вызов).
class QueueStream final : public Stream {
public:
  size_t read(uint8_t *buffer, size_t max_len) override {
    size_t n = 0;
    while (n < max_len) {
      uint8_t byte = 0;
      if (xQueueReceive(nmea_queue, &byte, 0) != pdTRUE)
        break;
      buffer[n++] = byte;
    }
    return n;
  }
};

// ============================================================================
// Время в миллисекундах (для анти-спам фильтра NMEAStreamReader)
// ============================================================================
static uint32_t get_time_ms() {
  return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// ============================================================================
// Задача-производитель: файл -> очередь
// ============================================================================
static void producer_task(void *) {
  FILE *file = std::fopen("data/sample.nmea", "rb");
  if (!file) {
    std::printf("Producer: cannot open data/sample.nmea "
                "(run from examples/freertos/)\n");
    vTaskDelete(nullptr);
    return;
  }

  int c;
  size_t sent = 0;
  while ((c = std::fgetc(file)) != EOF) {
    const uint8_t byte = static_cast<uint8_t>(c);
    if (xQueueSend(nmea_queue, &byte, portMAX_DELAY) != pdTRUE)
      break;
    ++sent;
  }
  std::fclose(file);

  std::printf("Producer: sent %zu bytes to queue\n", sent);
  vTaskDelete(nullptr);
}

// ============================================================================
// Задача-потребитель: очередь -> NMEAStreamReader -> Parser -> вывод
// ============================================================================
static void consumer_task(void *) {
  Parser parser;
  NMEAStreamReader reader;
  QueueStream stream;

  // После того как производитель отправил файл и завершился, очередь
  // опустеет. Несколько пустых итераций подряд -> завершение задачи,
  // после чего планировщик останавливается и процесс возвращает управление.
  uint32_t idle_rounds = 0;

  for (;;) {
    const size_t processed = reader.read_available(parser, stream, get_time_ms);
    if (processed > 0) {
      idle_rounds = 0;
      const NMEAData &data = parser.data();
      if (data.has_coordinates()) {
        char buf[256];
        to_txt(data, buf, sizeof(buf));
        std::printf("%s\n", buf);
      }
    } else if (++idle_rounds > 50) {
      auto stats = reader.get_stats();
      std::printf("Consumer: done. processed=%zu aborted=%zu "
                  "anti_spam_dropped=%zu\n",
                  stats.packets_processed, stats.packets_aborted,
                  stats.anti_spam_dropped);
      // POSIX-порт не завершает процесс сам, после удаления всех задач.
      std::exit(0);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================================
// main: создание очереди и задач, запуск планировщика
// ============================================================================
int main() {
  nmea_queue = xQueueCreate(512, sizeof(uint8_t));
  configASSERT(nmea_queue != nullptr);

  xTaskCreate(producer_task, "producer", 2048, nullptr, 1, nullptr);
  xTaskCreate(consumer_task, "consumer", 4096, nullptr, 2, nullptr);

  std::printf("LightNMEA FreeRTOS example: starting scheduler\n");
  vTaskStartScheduler();

  // Тут управление не возвращается (POSIX-порт завершает процесс).
  return 0;
}
