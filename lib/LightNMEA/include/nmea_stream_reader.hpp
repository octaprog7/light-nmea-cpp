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

#pragma once

#include "light_nmea.hpp"
#include "stream.hpp"

#include <cstddef>
#include <cstdint>

namespace light_nmea {

// Константы
constexpr size_t STREAM_MAX_CHUNK = 512;
constexpr size_t STREAM_LINE_BUFFER_SIZE = 256;
constexpr size_t STREAM_MIN_PACKET_LEN = 8;

constexpr uint8_t STREAM_DOLLAR = '$';
constexpr uint8_t STREAM_CR = '\r';
constexpr uint8_t STREAM_LF = '\n';

// Типы сообщений для анти-спам фильтра
enum class StreamMsgType : uint8_t { UNKNOWN = 0, GGA = 1, RMC = 2 };

// Определение типа сообщения (inline для header-only)
inline StreamMsgType get_msg_type(const uint8_t *buf, size_t len) noexcept {
  if (len < 7)
    return StreamMsgType::UNKNOWN;
  if (buf[3] == 'G' && buf[4] == 'G' && buf[5] == 'A')
    return StreamMsgType::GGA;
  if (buf[3] == 'R' && buf[4] == 'M' && buf[5] == 'C')
    return StreamMsgType::RMC;
  return StreamMsgType::UNKNOWN;
}

// Тип callback для получения времени (функция-указатель для zero-overhead)
using TimeFunc = uint32_t (*)();

// Тип callback для обработки пакетов (обычный указатель на функцию, без
// std::function)
using StreamCallback = void (*)(bool recognized, bool valid,
                                Constellation constellation);

class NMEAStreamReader {
public:
  NMEAStreamReader()
      : pos_(0), packets_processed_(0), packets_aborted_(0),
        anti_spam_dropped_(0), anti_spam_min_ms_(0), anti_spam_last_gga_(0),
        anti_spam_last_rmc_(0) {}

  void set_anti_spam_interval(uint32_t interval_ms) noexcept {
    anti_spam_min_ms_ = interval_ms;
  }

  void reset() noexcept {
    pos_ = 0;
    packets_processed_ = 0;
    packets_aborted_ = 0;
    anti_spam_dropped_ = 0;
    anti_spam_last_gga_ = 0;
    anti_spam_last_rmc_ = 0;
  }

  // Читает все доступные байты из абстрактного потока (UART, сокет, файл,
  // ...), формирует NMEA-пакеты и передаёт их парсеру.
  //
  // \param parser       Парсер, в который отправляются распознанные пакеты.
  // \param stream       Абстрактный источник байтов (потомок Stream).
  // \param get_time_ms  Функция получения текущего времени в мс (для
  //                     анти-спам фильтра).
  // \param callback     Опциональный обработчик каждого пакета:
  //                     callback(recognized, valid, constellation).
  // \return Количество успешно обработанных строк в этом вызове.
  size_t read_available(Parser &parser, Stream &stream, TimeFunc get_time_ms,
                        StreamCallback callback = nullptr) {
    uint8_t read_buffer[STREAM_MAX_CHUNK];
    size_t total_processed = 0;

    // Читаем в цикле, пока есть данные
    while (true) {
      size_t num_bytes = stream.read(read_buffer, STREAM_MAX_CHUNK);
      if (num_bytes == 0)
        break;

      size_t pos = pos_;

      for (size_t i = 0; i < num_bytes; ++i) {
        uint8_t byte = read_buffer[i];

        if (byte == STREAM_DOLLAR) {
          if (pos > 0)
            packets_aborted_++;
          pos = 0;
          line_buffer_[pos++] = byte;
        } else if (pos > 0) {
          if (pos < STREAM_LINE_BUFFER_SIZE) {
            line_buffer_[pos++] = byte;
          } else {
            packets_aborted_++;
            pos = 0;
            continue;
          }

          if (byte == STREAM_LF && pos >= 2 &&
              line_buffer_[pos - 2] == STREAM_CR) {
            if (pos >= STREAM_MIN_PACKET_LEN) {
              StreamMsgType msg_type = get_msg_type(line_buffer_, pos);

              if (msg_type != StreamMsgType::UNKNOWN &&
                  should_filter(msg_type, get_time_ms())) {
                anti_spam_dropped_++;
                pos = 0;
                continue;
              }

              bool recognized = parser.parse_line(std::string_view(
                  reinterpret_cast<const char *>(line_buffer_), pos));

              if (callback) {
                callback(recognized, parser.data().valid,
                         parser.data().constellation);
              }

              total_processed++;
            }
            pos = 0;
          }
        }
      }

      pos_ = pos;
    }

    packets_processed_ += total_processed;
    return total_processed;
  }

  struct Stats {
    size_t packets_processed;
    size_t packets_aborted;
    size_t anti_spam_dropped;
  };

  Stats get_stats() const noexcept {
    return {packets_processed_, packets_aborted_, anti_spam_dropped_};
  }

private:
  bool should_filter(StreamMsgType msg_type, uint32_t now_ms) noexcept {
    if (anti_spam_min_ms_ == 0)
      return false;

    if (msg_type == StreamMsgType::GGA) {
      if (now_ms - anti_spam_last_gga_ < anti_spam_min_ms_)
        return true;
      anti_spam_last_gga_ = now_ms;
    } else if (msg_type == StreamMsgType::RMC) {
      if (now_ms - anti_spam_last_rmc_ < anti_spam_min_ms_)
        return true;
      anti_spam_last_rmc_ = now_ms;
    }
    return false;
  }

  uint8_t line_buffer_[STREAM_LINE_BUFFER_SIZE];
  size_t pos_;

  size_t packets_processed_;
  size_t packets_aborted_;
  size_t anti_spam_dropped_;

  uint32_t anti_spam_min_ms_;
  uint32_t anti_spam_last_gga_;
  uint32_t anti_spam_last_rmc_;
};

} // namespace light_nmea