/*
 * LightNMEA - Ultra-fast NMEA GPS parser
 * Copyright (C) 2026 Roman Shevchik
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file main.cpp
 * @brief ESP32 (D1 mini) обёртка RTOS-бенчмарка NMEA-парсеров.
 *
 * Запускает bench_rtos::run_benchmark() в задаче FreeRTOS (ESP-IDF/Arduino).
 * Источник времени - esp_timer_get_time() (аппаратный таймер, 64-битные мкс).
 * Вывод - printf (перенаправлен на Serial UART0 ядром ESP32 Arduino).
 *
 * millis() для TinyGPSPlus предоставляет ядро Arduino (конфликта нет:
 * TinyGPS++.cpp компилируется с ARDUINO=1 и свою millis() он не определяет).
 *
 * Сборка: см. platformio.ini.
 *   pio run -t upload -e d1_mini32
 *   pio device monitor -b 115200
 */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <cstdio>

// Относительный путь: НЕ добавляю benchmarks/rtos в include path, чтобы
// Arduino.h-обёртка оттуда не заслонила настоящий Arduino.h ядра ESP32.
#include "../../bench_compare_rtos.hpp"

// ============================================================================
// Время в микросекундах (аппаратный таймер ESP32)
// ============================================================================
static uint64_t now_us() {
  return static_cast<uint64_t>(esp_timer_get_time());
}

// ============================================================================
// Задача-бенчмарк (приоритет 2: выше loopTask, чтобы замер не прерывался)
// ============================================================================
static void bench_task(void *) {
  const bench_rtos::BenchEnv env{now_us, std::printf};
  bench_rtos::run_benchmark(env);
  std::printf("Done. Reboot to re-run.\n");
  vTaskDelete(nullptr);
}

// ============================================================================
// Arduino entry points
// ============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  // Бенчмарк - длительная задача для CPU : отключаю task WDT, иначе
  // IDLE-задача не сбрасывает watchdog (по умолчанию 5 с).
  disableCore0WDT();
  disableCore1WDT();

  std::printf("LightNMEA ESP32 RTOS benchmark: starting FreeRTOS task\n");
  xTaskCreate(bench_task, "bench", 4096, nullptr, 2, nullptr);
}

void loop() {
  delay(1000);  // idle; бенчмарк работает в отдельной задаче FreeRTOS
}