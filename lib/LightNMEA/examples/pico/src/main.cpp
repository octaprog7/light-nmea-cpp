/*
 * LightNMEA Raspberry Pi Pico example
 *
 * Плата: Raspberry Pi Pico (RP2040, Cortex-M0+ @ 133 MHz).
 * GNSS-модуль G10F-F30 подключён к GP0/GP1 (UART0):
 *   GP0 (UART0 TX) -> RX модуля (опционально, для UBX/NMEA-команд)
 *   GP1 (UART0 RX) <- TX модуля (поток NMEA-0183, 38400 бод по умолчанию)
 *
 * Вывод распознанных данных — по USB CDC (порт "Pico" на хосте), чтобы
 * UART0 оставался полностью под GNSS. LED (GP25) мигает, когда есть данные.
 *
 * Демо-режим (сборка с -DLIGHTNMEA_DEMO): модуль не нужен совсем — встроенный
 * синтетический NMEA-трек подаётся в тот же конвейер Stream -> NMEAStreamReader
 * -> Parser -> to_txt, что и в реальном режиме. Удобно проверить прошивку
 * и связку библиотеки в помещении, без спутников.
 *
 * Ядро LightNMEA (light_nmea.hpp, nmea_stream_reader.hpp, nmea_formatter.hpp)
 * — чистый C++17 без зависимостей от Arduino: здесь Arduino используется
 * только как обвязка (Serial/Serial1/millis).
 *
 * Примечание: в namespace Arduino уже есть свой класс Stream, поэтому
 * типы LightNMEA пишутся с явной квалификацией light_nmea::...,
 * "using namespace light_nmea" здесь не используется во избежание
 * неоднозначности.
 */

#include <Arduino.h>

#include "light_nmea.hpp"
#include "nmea_formatter.hpp"
#include "nmea_stream_reader.hpp"

#ifndef GNSS_BAUD
#define GNSS_BAUD 9600
#endif
#ifndef GNSS_TX_PIN
#define GNSS_TX_PIN 0  // GP0
#endif
#ifndef GNSS_RX_PIN
#define GNSS_RX_PIN 1  // GP1
#endif
#ifndef LED_PIN
#define LED_PIN 25     // LED на плате Pico
#endif

// ============================================================================
// Stream поверх HardwareSerial (UART0, GNSS)
// ============================================================================
// NMEAStreamReader читает байты вызовом light_nmea::Stream::read().
class GnssStream final : public light_nmea::Stream {
public:
  size_t read(uint8_t *buffer, size_t max_len) override {
    size_t n = 0;
    while (n < max_len && Serial1.available() > 0) {
      buffer[n++] = static_cast<uint8_t>(Serial1.read());
    }
    return n;
  }
};

#ifdef LIGHTNMEA_DEMO
// ============================================================================
// Демо-режим: встроенный синтетический NMEA-трек вместо UART
// ============================================================================
// Классическая тестовая выборка (Мюнхен, из примеров/бенчей LightNMEA).
static const char DEMO_TRACK[] =
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
    "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39\r\n"
    "$GPVTG,084.4,T,087.2,M,022.4,N,041.4,K*4F\r\n"
    "$GPGLL,4807.038,N,01131.000,E,123519,A*3D\r\n";
static constexpr size_t DEMO_TRACK_LEN = sizeof(DEMO_TRACK) - 1;
static constexpr uint32_t DEMO_SENTENCE_INTERVAL_MS = 200;

// Число предложений в треке (для банера)
static constexpr unsigned demo_sentence_count() {
  unsigned n = 0;
  for (size_t i = 0; i < DEMO_TRACK_LEN; ++i) {
    if (DEMO_TRACK[i] == '\n') ++n;
  }
  return n;
}

// Отдаёт по одному предложению за вызов (пауза DEMO_SENTENCE_INTERVAL_MS),
// зацикленно. Тот же интерфейс light_nmea::Stream, что и у реального UART.
class DemoStream final : public light_nmea::Stream {
public:
  size_t read(uint8_t *buffer, size_t max_len) override {
    const uint32_t now = millis();
    if (now - last_ms_ < DEMO_SENTENCE_INTERVAL_MS) {
      return 0;
    }
    last_ms_ = now;
    size_t n = 0;
    while (n < max_len) {
      const char c = DEMO_TRACK[cursor_];
      buffer[n++] = static_cast<uint8_t>(c);
      cursor_++;
      if (c == '\n' || cursor_ >= DEMO_TRACK_LEN) {
        if (cursor_ >= DEMO_TRACK_LEN) {
          cursor_ = 0;  // трек закончился — начинаем заново
        }
        break;
      }
    }
    return n;
  }

private:
  size_t cursor_ = 0;
  uint32_t last_ms_ = 0;
};
#endif // LIGHTNMEA_DEMO

// ============================================================================
// Время в миллисекундах (для анти-спам фильтра NMEAStreamReader)
// ============================================================================
static uint32_t get_time_ms() { return millis(); }

static light_nmea::Parser parser;
static light_nmea::NMEAStreamReader reader;
#ifndef LIGHTNMEA_DEMO
static GnssStream gnss_stream;
#else
static DemoStream demo_stream;
#endif

static uint32_t last_rx_ms = 0;
static bool led_state = false;

void setup() {
  // Вывод — USB CDC (порт "Pico"); ждём подключения терминала до 4 с
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 4000)) {}

#ifndef LIGHTNMEA_DEMO
  // GNSS на UART0: GP0 TX, GP1 RX
  Serial1.setTX(GNSS_TX_PIN);
  Serial1.setRX(GNSS_RX_PIN);
  Serial1.begin(GNSS_BAUD);
#endif

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
#ifdef LIGHTNMEA_DEMO
  Serial.println("=== LightNMEA Pico example (DEMO mode: built-in track) ===");
  Serial.printf("Serving %u sentences every %u ms via the same "
                "Stream -> Parser pipeline\n",
                demo_sentence_count(), (unsigned)DEMO_SENTENCE_INTERVAL_MS);
#else
  Serial.println("=== LightNMEA Pico example (G10F-F30 on GP0/GP1) ===");
  Serial.printf("GNSS UART0: %d baud, TX GP%d / RX GP%d\n",
                GNSS_BAUD, GNSS_TX_PIN, GNSS_RX_PIN);
#endif
}

void loop() {
  const uint32_t now_ms = millis();

#ifdef LIGHTNMEA_DEMO
  light_nmea::Stream &input = demo_stream;
#else
  light_nmea::Stream &input = gnss_stream;
#endif
  const size_t processed = reader.read_available(parser, input, get_time_ms);
  if (processed > 0) {
    last_rx_ms = now_ms;
    // Моргаем LED на каждый пакет — наглядный признак потока данных
    led_state = !led_state;
    digitalWrite(LED_PIN, led_state ? HIGH : LOW);

    const light_nmea::NMEAData &data = parser.data();
    if (data.has_coordinates()) {
      char buf[256];
      light_nmea::to_txt(data, buf, sizeof(buf));
      Serial.println(buf);
    }
  }

#ifndef LIGHTNMEA_DEMO
  // Если данных нет дольше 5 секунд — сообщаем (LED гаснет).
  // В помещении модуль обычно продолжает слать предложения без фикса
  // (GGA fix=0 и т.п.) — тогда молчание означает проблему с проводкой/бодом.
  if (led_state && (now_ms - last_rx_ms > 5000UL)) {
    led_state = false;
    digitalWrite(LED_PIN, LOW);
    Serial.println("no GNSS data for 5 s (check wiring / baud rate)");
  }
#endif
}
