/*
 * avr_bench_common.hpp - общая bare-metal инфраструктура AVR-бенчмарков
 * (ATmega32L-8AU, avr5): USART0 (PD1 TX), Timer1 для micros()/millis(),
 * PROGMEM-пул NMEA-пакетов с копированием в SRAM.
 *
 * Почему копирование в SRAM: на классическом AVR constexpr/const-данные лежат
 * во flash, а динамическая индексация через указатели (std::string_view /
 * std::array) генерирует LD (чтение SRAM), а не LPM — данные читались бы
 * неверно. После копирования парсеры работают с обычным SRAM-буфером,
 * как при реальном приёме из UART.
 *
 * Содержимое пакетов - зеркало nav_gen::NAV_PACKETS для индексов PKT_IDX
 * из bench_compare_mcu.cpp (0,1,2,3,19,20,22,23). Единственный источник
 * истины по тексту пакетов - nav_gen.hpp; синхронизируйте оба файла.
 */
#ifndef LIGHTNMEA_AVR_BENCH_COMMON_HPP
#define LIGHTNMEA_AVR_BENCH_COMMON_HPP

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>

#include <array>
#include <cstdint>
#include <string_view>

#ifndef F_CPU
#error "F_CPU must be defined, e.g. -DF_CPU=8000000UL"
#endif

// Частота тика таймера micros(): Timer1 после деления на 8.
// Тик = max(1, F_CPU/8e6) мкс: 1.0 мкс @8 МГц, 2.0 мкс @16 МГц, 8 мкс @1 МГц.
constexpr uint32_t TIMER_TICK_US =
    ((F_CPU / 8UL) > 1000000UL) ? ((F_CPU / 8UL) / 1000000UL) : 1UL;

// ============================================================================
// Пакеты NMEA в PROGMEM
// ============================================================================
static const char PKT0[] PROGMEM = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";
static const char PKT1[] PROGMEM = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
static const char PKT2[] PROGMEM = "$GNRMC,091530,A,5545.1234,N,03737.5678,E,015.0,120.5,220626,,,A*69\r\n";
static const char PKT3[] PROGMEM = "$GNGGA,091530.00,5545.1234,N,03737.5678,E,1,12,0.8,156.3,M,14.2,M,,*78\r\n";
static const char PKT4[] PROGMEM = "$GPVTG,084.4,T,081.3,M,022.4,N,041.5,K,A*25\r\n";
static const char PKT5[] PROGMEM = "$GNVTG,120.5,T,117.4,M,015.0,N,027.8,K,A*31\r\n";
static const char PKT6[] PROGMEM = "$GPGLL,4807.038,N,01131.000,E,123519,A,A*48\r\n";
static const char PKT7[] PROGMEM = "$GNGLL,5545.1234,N,03737.5678,E,091530,A,A*5E\r\n";

static const char *const PACKETS_P[] PROGMEM = {PKT0, PKT1, PKT2, PKT3, PKT4, PKT5, PKT6, PKT7};
static constexpr uint8_t NUM_PACKETS = 8;

// Флеш-пул под копии пакетов в SRAM (~460 байт суммарно).
static char g_pool[512];
// Вид пакетов для парсеров (указывают в g_pool, обычный SRAM).
static std::array<std::string_view, NUM_PACKETS> g_pkts;

// Sink: мешает компилятору выбросить "бесполезные" горячие циклы.
static volatile uint32_t g_sink = 0;

// ============================================================================
// USART0 (TX-only), U2X, автовыбор бода по минимальной ошибке.
// При 8 МГц - 38400 (ошибка 0.16%), при 16 МГц - 115200.
// ============================================================================
namespace uart_cfg {
constexpr uint32_t pick_baud() {
  constexpr uint32_t candidates[] = {115200UL, 57600UL, 38400UL, 19200UL, 9600UL};
  uint32_t best = 9600UL;
  uint32_t best_err = 0xFFFFFFFFUL;
  for (uint32_t b : candidates) {
    const uint32_t u = F_CPU / (8UL * b);  // U2X divisor
    if (u < 2UL) continue;
    const uint32_t actual = F_CPU / (8UL * (u - 1UL));
    uint32_t err = (actual > b) ? (actual - b) : (b - actual);
    err = err * 1000UL / b;
    if (err < best_err) {
      best_err = err;
      best = b;
    }
  }
  return best;
}
constexpr uint32_t baud() { return pick_baud(); }
constexpr uint16_t ubrr() { return static_cast<uint16_t>((F_CPU / (8UL * baud())) - 1UL); }
} // namespace uart_cfg

static inline void uart_init() {
  const uint16_t u = uart_cfg::ubrr();
  UCSRA = (1 << U2X);
  UBRRH = static_cast<uint8_t>(u >> 8);
  UBRRL = static_cast<uint8_t>(u);
  UCSRB = (1 << TXEN);
  UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);  // 8N1
}

static inline void uart_putc(char c) {
  while (!(UCSRA & (1 << UDRE))) {}
  UDR = static_cast<uint8_t>(c);
}

static inline void puts_pgm(const char *s) {
  char c;
  while ((c = static_cast<char>(pgm_read_byte(s++))) != '\0') uart_putc(c);
}

static inline void put_u32(uint32_t v) {
  char buf[10];
  uint8_t n = 0;
  do { buf[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
  while (n) uart_putc(buf[--n]);
}

// Число, выровненное вправо в поле заданной ширины.
static inline void put_u32_fmt(uint32_t v, uint8_t width) {
  char buf[10];
  uint8_t n = 0;
  do { buf[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
  for (uint8_t i = n; i < width; ++i) uart_putc(' ');
  while (n) uart_putc(buf[--n]);
}

static inline void nl() {
  uart_putc('\r');
  uart_putc('\n');
}

// ============================================================================
// Timer1, прескалер /8: тик = TIMER_TICK_US мкс, 32-битное расширение.
// ============================================================================
static volatile uint32_t g_t1_ticks;  // суммарные тики Timer1 (32 бита)

static inline void timer_init() {
  TCNT1 = 0;
  TCCR1B = (1 << CS11);  // clk/8
  TIMSK = (1 << TOIE1);
  g_t1_ticks = 0;
  sei();
}

static inline uint32_t micros32() {
  const uint8_t sreg = SREG;
  cli();
  const uint16_t t = TCNT1;
  uint32_t ticks = g_t1_ticks;
  // TOV1 установлен, а ISR ещё не отработала (TCNT1 только-только прошёл 0):
  // досчитываем недостающий период. Эвристика безопасна - других прерываний нет.
  if ((TIFR & (1 << TOV1)) && t < 1024UL) {
    ticks += 65536UL;
  }
  SREG = sreg;
  return ticks * TIMER_TICK_US;
}

// Требуется TinyGPSPlus (см. src/Arduino.h)
inline unsigned long millis() { return micros32() / 1000UL; }

// ============================================================================
// Загрузка пакетов из PROGMEM в SRAM-пул
// ============================================================================
static inline void init_packets() {
  size_t off = 0;
  for (uint8_t i = 0; i < NUM_PACKETS; ++i) {
    const char *p = reinterpret_cast<const char *>(pgm_read_word(&PACKETS_P[i]));
    const uint16_t n = static_cast<uint16_t>(strlen_P(p));
    if (off + n > sizeof(g_pool)) {
      puts_pgm(PSTR("packet pool overflow\r\n"));
      for (;;) {}
    }
    memcpy_P(g_pool + off, p, n);
    g_pkts[i] = std::string_view(g_pool + off, n);
    off += n;
  }
}

#endif // LIGHTNMEA_AVR_BENCH_COMMON_HPP