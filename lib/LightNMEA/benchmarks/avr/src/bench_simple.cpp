/*
/*
 * bench_simple.cpp - упрощённый on-target бенчмарк LightNMEA для AVR
 *
 * Парсит пакеты через light_nmea::Parser, выводит метрику: pkt/s.
 *
 * Output: USART0 TX (PD1), один запуск на бут.
 * Build: BENCH=simple ./build.sh
 *
 * Исправление: используется micros32() из avr_bench_common.hpp с Timer1/8
 * и 32-битным расширением для корректного измерения длительных интервалов.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>

#include <cstdint>

#include "light_nmea.hpp"
#include "avr_bench_common.hpp"

// ============================================================================
// Конфигурация
// ============================================================================
#ifndef F_CPU
#error "F_CPU must be defined, e.g. -DF_CPU=8000000UL"
#endif
#ifndef BENCH_PACKET_COUNT
#define BENCH_PACKET_COUNT 10000UL
#endif

// ============================================================================
// Вспомогательные функции вывода
// ============================================================================
static void print_header() {
    puts_pgm(PSTR("\r\n=== LightNMEA Simple Benchmark (AVR) ===\r\n"));
}

// ============================================================================
// Простая задержка через Timer1 (после timer_init())
// ============================================================================
static void delay_ms(uint16_t ms) {
    uint32_t start = micros32();
    while ((micros32() - start) < (uint32_t)ms * 1000UL);
}

// ============================================================================
// Основной бенчмарк
// ============================================================================
int main() {
    uart_init();
    init_packets();

    print_header();

    puts_pgm(PSTR("    MCU: ATmega32L @ "));
    put_u32(F_CPU / 1000000UL);
    puts_pgm(PSTR(" MHz"));
    nl();
    puts_pgm(PSTR("    Packets: "));
    put_u32(NUM_PACKETS);
    puts_pgm(PSTR(", per pass: "));
    put_u32(BENCH_PACKET_COUNT);
    nl();
    nl();

    uint32_t pass = 0;

    // Инициализируем Timer1 для micros32() один раз
    timer_init();

    while (1) {
        uint32_t start_us = micros32();

        uint32_t total_packets = 0;
        light_nmea::Parser parser;
        light_nmea::Parser::Config config{false, false};

        uint32_t pkt_idx = 0;
        while (total_packets < BENCH_PACKET_COUNT) {
            const char* pkt = g_pkts[pkt_idx].data();  // C‑строка
            bool ok = parser.parse_line(pkt);
            if (ok) {
                g_sink += parser.data().has_lat_lon ? 1 : 0;
            }
            ++total_packets;
            ++pkt_idx;
            if (pkt_idx >= NUM_PACKETS) {
                pkt_idx = 0;
            }
        }

        uint32_t end_us = micros32();
        uint32_t elapsed_us = end_us - start_us;

        puts_pgm(PSTR("Pass "));
        put_u32(++pass);
        puts_pgm(PSTR(": "));

        uint32_t pkt_s = (elapsed_us > 0) ? (total_packets * 1000000UL / elapsed_us) : 0;
        puts_pgm(PSTR("pkt/s: "));
        put_u32(pkt_s);
        puts_pgm(PSTR(", elapsed_us: "));
        put_u32(elapsed_us);
        nl();

        delay_ms(500);
        nl();
    }

    return 0;
}
