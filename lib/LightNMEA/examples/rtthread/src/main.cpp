/*
 * LightNMEA RT-Thread example
 *
 * Реализует интеграцию LightNMEA с RT-Thread:
 *   - байты NMEA-потока поступают из UART-девайса (rt_device, прерывание RX
 *     + семафор rx_indicate) либо из встроенного симулятора-feed
 *     (режим LIGHTNMEA_SIM_FEED, удобно для qemu-vexpress-a9 без GPS);
 *   - байты копятся в кольцевом буфере;
 *   - в потоке main (главный поток RT-Thread) NMEAStreamReader собирает
 *     предложения, Parser парсит их, результат печатается через rt_kprintf.
 *
 * Ядро LightNMEA (light_nmea.hpp, nmea_stream_reader.hpp, nmea_formatter.hpp)
 * не зависит от Arduino/POSIX, поэтому работает в RT-Thread без изменений.
 *
 * Сборка: см. SConscript / Kconfig и README.md.
 */

#include <rtdevice.h>
#include <rtthread.h>

#include "nmea_formatter.hpp"
#include "nmea_stream_reader.hpp"

using namespace light_nmea;

// ============================================================================
// Конфигурация (Kconfig: LIGHTNMEA_UART_NAME, LIGHTNMEA_SIM_FEED)
// ============================================================================
#ifndef LIGHTNMEA_UART_NAME
#define LIGHTNMEA_UART_NAME "uart1"
#endif

// ============================================================================
// Кольцевой буфер для байт NMEA-потока
// ============================================================================
#define RING_SIZE 512
static uint8_t ring_data[RING_SIZE];
static volatile uint16_t ring_head = 0;
static volatile uint16_t ring_tail = 0;

static void ring_push(uint8_t byte) {
  const uint16_t next = static_cast<uint16_t>((ring_head + 1) % RING_SIZE);
  if (next != ring_tail) {  // буфер не переполнен: байты молча теряются
    ring_data[ring_head] = byte;
    ring_head = next;
  }
}

// ============================================================================
// Stream поверх кольцевого буфера
// ============================================================================
// NMEAStreamReader читает байты вызовом Stream::read() (неблокирующий метод).
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
// Время в миллисекундах (для анти-спам фильтра NMEAStreamReader)
// ============================================================================
static uint32_t get_time_ms() { return rt_tick_get_millisecond(); }

// ============================================================================
// Семафор: RX-прерывание / симулятор -> поток main
// ============================================================================
static rt_sem_t rx_sem = RT_NULL;

#ifndef LIGHTNMEA_SIM_FEED
// ============================================================================
// Режим реального UART: rt_device + rx_indicate
// ============================================================================
static rt_device_t serial = RT_NULL;

// Колбэк rx_indicate: вызывается из прерывания драйвера serial, когда в буфере
// есть новые данные. Только освобождает семафор — вся работа в потоке main.
static rt_err_t uart_input(rt_device_t dev, rt_size_t size) {
  (void)dev;  // колбэк rx_indicate: контекст не нужен, размер намекает на наличие данных
  (void)size;
  rt_sem_release(rx_sem);
  return RT_EOK;
}

// Дозагрузка данных из драйвера serial в кольцевой буфер
static void uart_drain() {
  uint8_t buf[64];
  rt_size_t n;
  while ((n = rt_device_read(serial, 0, buf, sizeof(buf))) > 0) {
    for (rt_size_t i = 0; i < n; ++i) {
      ring_push(buf[i]);
    }
  }
}
#endif // LIGHTNMEA_SIM_FEED

#ifdef LIGHTNMEA_SIM_FEED
// ============================================================================
// Режим симуляции: поток-производитель отправляет встроенные предложения NMEA
// (для BSP без GPS: qemu-vexpress-a9 и любые платы в "стендовых" тестах)
// ============================================================================
static const char *const SIM_SENTENCES[] = {
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n",
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n",
    "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39\r\n",
    "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75\r\n",
    "$GPVTG,084.4,T,087.2,M,022.4,N,041.4,K*4F\r\n",
    "$GPGLL,4807.038,N,01131.000,E,123519,A*3D\r\n",
};

static void sim_producer(void *) {
  for (int rep = 0; rep < 30; ++rep) {  // 30 повторов трека, потом стоп
    for (const char *s : SIM_SENTENCES) {
      while (*s != '\0') {
        ring_push(static_cast<uint8_t>(*s++));
      }
      rt_sem_release(rx_sem);
      rt_thread_mdelay(10);
    }
  }
  rt_kprintf("LightNMEA sim: producer finished\n");
}
#endif // LIGHTNMEA_SIM_FEED

// ============================================================================
// main (главный поток RT-Thread): ожидание данных -> парсинг -> вывод
// ============================================================================
int main(void) {
  Parser parser;
  NMEAStreamReader reader;
  RingStream stream;

  rx_sem = rt_sem_create("ln_rx", 0, RT_IPC_FLAG_FIFO);
  if (rx_sem == RT_NULL) {
    rt_kprintf("LightNMEA: cannot create semaphore\n");
    return -RT_ERROR;
  }

#ifdef LIGHTNMEA_SIM_FEED
  rt_thread_t producer = rt_thread_create("ln_sim", sim_producer, RT_NULL,
                                          1024, RT_THREAD_PRIORITY_MAX - 2, 10);
  if (producer == RT_NULL) {
    rt_kprintf("LightNMEA: cannot create producer thread\n");
    return -RT_ERROR;
  }
  rt_thread_startup(producer);
  rt_kprintf("LightNMEA RT-Thread example started (sim feed)\n");
#else
  serial = rt_device_find(LIGHTNMEA_UART_NAME);
  if (serial == RT_NULL) {
    rt_kprintf("LightNMEA: serial device '%s' not found\n",
               LIGHTNMEA_UART_NAME);
    return -RT_ERROR;
  }
  if (rt_device_open(serial, RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
    rt_kprintf("LightNMEA: cannot open '%s'\n", LIGHTNMEA_UART_NAME);
    return -RT_ERROR;
  }
  rt_device_set_rx_indicate(serial, uart_input);
  rt_kprintf("LightNMEA RT-Thread example started (UART: %s)\n",
             LIGHTNMEA_UART_NAME);
#endif

  for (;;) {
    // Ждём уведомления о новых данных, затем читаем всё доступное.
    rt_sem_take(rx_sem, RT_WAITING_FOREVER);
#ifndef LIGHTNMEA_SIM_FEED
    uart_drain();
#endif

    const size_t processed = reader.read_available(parser, stream, get_time_ms);
    if (processed > 0) {
      const NMEAData &data = parser.data();
      if (data.has_coordinates()) {
        char buf[256];
        to_txt(data, buf, sizeof(buf));
        rt_kprintf("%s\n", buf);
      }
    }
  }

  return RT_EOK;
}
