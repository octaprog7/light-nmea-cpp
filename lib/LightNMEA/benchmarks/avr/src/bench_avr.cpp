/*
 * bench_avr.cpp - on-target comparative NMEA parser benchmark
 * for classic 8-bit AVR (ATmega32L-8AU, avr5), bare-metal.
 *
 * Compares three parsers on identical packets (mirror of nav_gen.hpp PKT_IDX):
 *   - LightNMEA   (light_nmea.hpp, header-only, integer-only)
 *   - minmea      (kosma/minmea, plain C)
 *   - TinyGPSPlus (mikalhart/TinyGPSPlus, streaming)
 *
 * Why not bench_compare_mcu.cpp as-is on AVR?
 *   Classic AVR (avr5, no RAMPZ) has a separate flash address space: constexpr
 *   / const globals live in flash, but runtime indexing through pointers
 *   (std::string_view / std::array) emits LD (SRAM reads), not LPM (flash
 *   reads). nav_gen::NAV_PACKETS would therefore be silently misread.
 *   → The 8 packets used by PKT_IDX are mirrored here as PROGMEM arrays and
 *     copied into a RAM pool at startup; afterwards all three parsers read
 *     plain SRAM, exactly like a real UART RX buffer.
 *
 * Output: USART0 TX (PD1), one run per boot, reboot to re-run.
 * Metric: pkt/s, ns/pkt, cycles/pkt (rounded), pkt/s per MHz, best-of-N.
 *
 * Build: ./build.sh [build|flash|fuses|monitor|clean|help]  (see benchmarks/avr/)
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "light_nmea.hpp"

// Vendor parsers (reused from benchmarks/firmware/lib, no copies here)
#include "minmea.h"
#include "TinyGPS++.h"

// ============================================================================
// Configuration (override via -D on the build command line)
// ============================================================================
#ifndef F_CPU
#error "F_CPU must be defined, e.g. -DF_CPU=8000000UL"
#endif
#ifndef BENCH_PACKET_COUNT
#define BENCH_PACKET_COUNT 10000UL  // packets per pass (AVR is ~100x slower than RP2040)
#endif
#ifndef BENCH_PASSES
#define BENCH_PASSES 3  // best-of-N passes
#endif

// Аппаратная частота микросекундного таймера: Timer1 после деления на 8.
// Тик = max(1, F_CPU/8e6) мкс: 1.0 мкс @8 MHz, 2.0 мкс @16 MHz, 8 мкс @1 MHz.
constexpr uint32_t TIMER_TICK_US =
    ((F_CPU / 8UL) > 1000000UL) ? ((F_CPU / 8UL) / 1000000UL) : 1UL;

// ============================================================================
// Пакеты NMEA в PROGMEM (зеркало nav_gen::NAV_PACKETS[*] для индексов PKT_IDX
// из bench_compare_mcu.cpp: 0,1,2,3,19,20,22,23). Единственный источник истины
// по содержимому — nav_gen.hpp; при правках синхронизируйте оба файла.
// ============================================================================
static const char PKT0[] PROGMEM = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";
static const char PKT1[] PROGMEM = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
static const char PKT2[] PROGMEM = "$GNRMC,091530,A,5545.1234,N,03737.5678,E,015.0,120.5,220626,,,A*69\r\n";
static const char PKT3[] PROGMEM = "$GNGGA,091530.00,5545.1234,N,03737.5678,E,1,12,0.8,156.3,M,14.2,M,,*78\r\n";
static const char PKT4[] PROGMEM = "$GPVTG,084.4,T,081.3,M,022.4,N,041.5,K,A*25\r\n";
static const char PKT5[] PROGMEM = "$GNVTG,120.5,T,117.4,M,015.0,N,027.8,K,A*31\r\n";
static const char PKT6[] PROGMEM = "$GPGLL,4807.038,N,01131.000,E,123519,A,A*48\r\n";
static const char PKT7[] PROGMEM = "$GNGLL,5545.1234,N,03737.5678,E,091530,A,A*5E\r\n";

// Массив указателей тоже во flash (avr5: указатель = 2 байта).
static const char *const PACKETS_P[] PROGMEM = {PKT0, PKT1, PKT2, PKT3, PKT4, PKT5, PKT6, PKT7};
static constexpr uint8_t NUM_PACKETS = 8;

// Флеш-пул под копии пакетов в SRAM (суммарная длина ~460 байт + запас).
static char g_pool[512];
// Вид пакетов, который видят парсеры (указываюет в g_pool, обычный SRAM).
static std::array<std::string_view, NUM_PACKETS> g_pkts;

// Sink: мешает компилятору выбросить "бесполезные" горячие циклы.
static volatile uint32_t g_sink = 0;

// ============================================================================
// USART0 (TX-only), U2X, автовыбор бода с минимальной ошибкой.
// При 8 МГц выбирается 38400 (ошибка 0.16%), при 16 МГц — 115200.
// ============================================================================
namespace uart_cfg {
constexpr uint32_t pick_baud() {
  constexpr uint32_t candidates[] = {115200UL, 57600UL, 38400UL, 19200UL, 9600UL};
  uint32_t best = 9600UL;
  uint32_t best_err = 0xFFFFFFFFUL;
  for (uint32_t b : candidates) {
    const uint32_t u = F_CPU / (8UL * b);        // U2X divisor (rounded up)
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

static void uart_init() {
  const uint16_t u = uart_cfg::ubrr();
  UCSRA = (1 << U2X);
  UBRRH = static_cast<uint8_t>(u >> 8);
  UBRRL = static_cast<uint8_t>(u);
  UCSRB = (1 << TXEN);
  UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);  // 8N1
}

static void uart_putc(char c) {
  while (!(UCSRA & (1 << UDRE))) {}
  UDR = static_cast<uint8_t>(c);
}

static void puts_pgm(const char *s) {
  char c;
  while ((c = static_cast<char>(pgm_read_byte(s++))) != '\0') uart_putc(c);
}

static void put_u32(uint32_t v) {
  char buf[10];
  uint8_t n = 0;
  do { buf[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
  while (n) uart_putc(buf[--n]);
}

// Число, выровненное вправо в поле заданной ширины.
static void put_u32_fmt(uint32_t v, uint8_t width) {
  char buf[10];
  uint8_t n = 0;
  do { buf[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
  for (uint8_t i = n; i < width; ++i) uart_putc(' ');
  while (n) uart_putc(buf[--n]);
}

// Название из PROGMEM, дополненное пробелами до 12 символов.
static void put_name_pgm(const char *s) {
  uint8_t n = 0;
  while (n < 12) {
    const char c = static_cast<char>(pgm_read_byte(s++));
    if (c == '\0') break;
    uart_putc(c);
    ++n;
  }
  while (n < 12) { uart_putc(' '); ++n; }
}

static void nl() {
  uart_putc('\r');
  uart_putc('\n');
}

// ============================================================================
// Timer1, прескалер /8: тик = TIMER_TICK_US мкс. 32-битное расширение через ISR.
// ============================================================================
static volatile uint32_t g_t1_ticks;  // суммарное число тиков Timer1 (32 бита)

ISR(TIMER1_OVF_vect) {
  g_t1_ticks += 65536UL;  // один период 16-битного счётчика
}

static void timer_init() {
  TCNT1 = 0;
  TCCR1B = (1 << CS11);          // clk/8
  TIMSK = (1 << TOIE1);
  g_t1_ticks = 0;
  sei();
}

static uint32_t micros32() {
  const uint8_t sreg = SREG;
  cli();
  const uint16_t t = TCNT1;
  uint32_t ticks = g_t1_ticks;
  // TOV1 установлен, а ISR ещё не отработала (TCNT1 только-только прошёл 0) —
  // добавляем недостающий период. Эвристика безопасна: других прерываний нет.
  if ((TIFR & (1 << TOV1)) && t < 1024UL) {
    ticks += 65536UL;
  }
  SREG = sreg;
  return ticks * TIMER_TICK_US;
}

unsigned long millis() {
  return micros32() / 1000UL;
}

// ============================================================================
// Загрузка пакетов из PROGMEM в SRAM-пул
// ============================================================================
static void init_packets() {
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

// ============================================================================
// Горячие циклы (идентичны bench_compare_mcu.cpp, данные — из g_pkts в SRAM)
// ============================================================================
enum class Impl : uint8_t { LIGHT, MINMEA, TINYGPS };

static uint32_t run_lightnmea(uint32_t total) noexcept {
  light_nmea::Parser parser;
  uint32_t ok = 0;
  for (uint32_t i = 0; i < total; ++i) {
    if (parser.parse_line(g_pkts[i % NUM_PACKETS])) {
      ++ok;
    }
  }
  g_sink += ok;
  return ok;
}

static uint32_t run_minmea(uint32_t total) noexcept {
  uint32_t ok = 0;
  for (uint32_t i = 0; i < total; ++i) {
    const char *line = g_pkts[i % NUM_PACKETS].data();
    const char t1 = line[3], t2 = line[4], t3 = line[5];
    if (t1 == 'R' && t2 == 'M' && t3 == 'C') {
      minmea_sentence_rmc f{};
      if (minmea_parse_rmc(&f, line)) ++ok;
    } else if (t1 == 'G' && t2 == 'G' && t3 == 'A') {
      minmea_sentence_gga f{};
      if (minmea_parse_gga(&f, line)) ++ok;
    } else if (t1 == 'V' && t2 == 'T' && t3 == 'G') {
      minmea_sentence_vtg f{};
      if (minmea_parse_vtg(&f, line)) ++ok;
    } else if (t1 == 'G' && t2 == 'L' && t3 == 'L') {
      minmea_sentence_gll f{};
      if (minmea_parse_gll(&f, line)) ++ok;
    }
  }
  g_sink += ok;
  return ok;
}

static uint32_t run_tinygps(uint32_t total) noexcept {
  TinyGPSPlus gps;
  uint32_t ok = 0;
  for (uint32_t i = 0; i < total; ++i) {
    const std::string_view sv = g_pkts[i % NUM_PACKETS];
    for (const char c : sv) {
      if (gps.encode(c)) ++ok;  // encode() == true on a completed sentence
    }
  }
  g_sink += ok;
  return ok;
}

static uint32_t run_impl(Impl impl, uint32_t total) noexcept {
  switch (impl) {
    case Impl::LIGHT: return run_lightnmea(total);
    case Impl::MINMEA: return run_minmea(total);
    case Impl::TINYGPS: return run_tinygps(total);
  }
  return 0;
}

// ============================================================================
// Замер (best-of-N) и результат
// ============================================================================
struct BenchResult {
  const char *name;  // PROGMEM-строка
  uint32_t pkt_per_s = 0;
  uint32_t ns_per_pkt = 0;
  uint32_t cycles_per_pkt = 0;     // округлённые циклы на пакет
  uint32_t pkt_per_s_per_mhz = 0;  // pkt/s на 1 МГц
  uint32_t ok = 0;
};

static BenchResult bench_impl(Impl impl, const char *name) noexcept {
  uint32_t best_dt = 0xFFFFFFFFu;
  uint32_t best_ok = 0;
  for (uint32_t pass = 0; pass < BENCH_PASSES; ++pass) {
    const uint32_t t0 = micros32();
    const uint32_t ok = run_impl(impl, BENCH_PACKET_COUNT);
    const uint32_t dt = micros32() - t0;
    if (dt < best_dt) {
      best_dt = dt;
      best_ok = ok;
    }
  }
  if (best_dt == 0) best_dt = 1;

  const uint32_t mhz = static_cast<uint32_t>(F_CPU) / 1000000UL;
  BenchResult r;
  r.name = name;
  r.pkt_per_s = static_cast<uint32_t>(
      static_cast<uint64_t>(BENCH_PACKET_COUNT) * 1000000ULL / best_dt);
  r.ns_per_pkt = best_dt * 1000UL / BENCH_PACKET_COUNT;
  r.cycles_per_pkt = (r.ns_per_pkt * mhz + 500UL) / 1000UL;
  r.pkt_per_s_per_mhz = r.pkt_per_s / mhz;
  r.ok = best_ok;
  return r;
}

static const char NAME_LIGHT[] PROGMEM = "LightNMEA";
static const char NAME_MINMEA[] PROGMEM = "minmea";
static const char NAME_TINYGPS[] PROGMEM = "TinyGPSPlus";

static void print_result(const BenchResult &r) noexcept {
  put_name_pgm(r.name);
  put_u32_fmt(r.pkt_per_s, 10); uart_putc(' ');
  put_u32_fmt(r.ns_per_pkt, 8); uart_putc(' ');
  put_u32_fmt(r.cycles_per_pkt, 10); uart_putc(' ');
  put_u32_fmt(r.pkt_per_s_per_mhz, 10); uart_putc(' ');
  put_u32(r.ok); uart_putc('/'); put_u32(BENCH_PACKET_COUNT);
  nl();
}

// ============================================================================
// Тексты вывода (PROGMEM)
// ============================================================================
static const char T_BANNER[] PROGMEM = "=== LightNMEA MCU comparative benchmark (8-bit AVR) ===";
static const char T_CFG_PACKETS[] PROGMEM = "Packets/pass: ";
static const char T_CFG_PASSES[] PROGMEM = ", passes: ";
static const char T_CFG_BEST[] PROGMEM = " (best), F_CPU: ";
static const char T_CFG_HZ[] PROGMEM = " Hz, UART baud: ";
static const char T_HDR[] PROGMEM = "Implementation  pkt/s       ns/pkt  cycles/pkt  pkt/s/MHz  ok/total";
static const char T_DONE[] PROGMEM = "Done. Reboot the board to re-run.";

int main() {
  uart_init();
  timer_init();
  init_packets();

  nl();
  puts_pgm(T_BANNER); nl();
  puts_pgm(T_CFG_PACKETS); put_u32(BENCH_PACKET_COUNT);
  puts_pgm(T_CFG_PASSES); put_u32(BENCH_PASSES);
  puts_pgm(T_CFG_BEST); put_u32(F_CPU);
  puts_pgm(T_CFG_HZ); put_u32(uart_cfg::baud());
  nl(); nl();

  const BenchResult rl = bench_impl(Impl::LIGHT, NAME_LIGHT);
  const BenchResult rm = bench_impl(Impl::MINMEA, NAME_MINMEA);
  const BenchResult rt = bench_impl(Impl::TINYGPS, NAME_TINYGPS);

  puts_pgm(T_HDR); nl();
  print_result(rl);
  print_result(rm);
  print_result(rt);
  nl();
  puts_pgm(T_DONE); nl();

  for (;;) {}  // after measurement; reboot to re-run
}