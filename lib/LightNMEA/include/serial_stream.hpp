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

#include "stream.hpp"

#include <cstddef>
#include <cstdint>

// SerialStream работает только на POSIX-системах (Linux/macOS/BSD).
#if defined(__unix__) || defined(__APPLE__)

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace light_nmea {

// ============================================================================
// Поток из последовательного порта (UART) на POSIX.
// ============================================================================
// Открывает устройство вида "/dev/ttyUSB0", "/dev/ttyS0", "/dev/ttyAMA0"
// и настраивает его как "сырой" (raw) порт для NMEA-данных.
//
// Соединяется с NMEAStreamReader через абстрактный интерфейс Stream:
//   Parser parser;
//   NMEAStreamReader reader;
//   SerialStream uart;
//   if (uart.open("/dev/ttyUSB0", 9600)) {
//       reader.read_available(parser, uart, get_time_ms);
//   }
//
// read() - неблокирующий: при отсутствии данных возвращает 0 сразу.
class SerialStream final : public Stream {
public:
  SerialStream() noexcept = default;
  ~SerialStream() override { close(); }

  SerialStream(const SerialStream &) = delete;
  SerialStream &operator=(const SerialStream &) = delete;

  // Открывает последовательный порт и настраивает его под NMEA.
  // Возвращает true при успехе. Поддерживаемые скорости задаются в baud.
  bool open(const char *device, uint32_t baud) noexcept {
    close();

    fd_ = ::open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0)
      return false;

    const speed_t speed = to_speed_t(baud);
    if (speed == B0) {
      close();
      return false;
    }

    termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
      close();
      return false;
    }

    ::cfmakeraw(&tty); // сырой режим: байты передаются без преобразований

    // Неблокирующее чтение: VMIN=0, VTIME=0
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0) {
      close();
      return false;
    }

    // Сброс буферов и применение настроек
    ::tcflush(fd_, TCIOFLUSH);
    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
      close();
      return false;
    }

    return true;
  }

  void close() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  // Сбрасывает (выбрасывает) все непрочитанные байты из входного буфера.
  // Полезно после смены скорости/настройки линии или для очистки USB-CDC.
  void flush_input() noexcept {
    if (fd_ >= 0)
      ::tcflush(fd_, TCIFLUSH);
  }

  // Управляет сигналами DTR/RTS (например, для "пробуждения" USB-CDC на
  // RP2040: dtr=false, rts=false -> пауза -> dtr=true, rts=true).
  // Возвращает true при успехе.
  bool set_dtr_rts(bool dtr, bool rts) noexcept {
    if (fd_ < 0)
      return false;
    int status = 0;
    if (::ioctl(fd_, TIOCMGET, &status) != 0)
      return false;
    if (dtr)
      status |= TIOCM_DTR;
    else
      status &= ~TIOCM_DTR;
    if (rts)
      status |= TIOCM_RTS;
    else
      status &= ~TIOCM_RTS;
    return ::ioctl(fd_, TIOCMSET, &status) == 0;
  }

  [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

  // Реализация абстрактного Stream::read().
  // Неблокирующая: возвращает 0, если данных в порту сейчас нет.
  size_t read(uint8_t *buffer, size_t max_len) override {
    if (fd_ < 0)
      return 0;
    ssize_t n = ::read(fd_, buffer, max_len);
    if (n <= 0)
      return 0; // EAGAIN / EOF / ошибка -> конец текущего чтения
    return static_cast<size_t>(n);
  }

  // Блокирующее чтение одного фрагмента (может ждать данные).
  // Удобно для отдельного потока чтения.
  size_t read_blocking(uint8_t *buffer, size_t max_len) noexcept {
    if (fd_ < 0)
      return 0;
    ssize_t n = ::read(fd_, buffer, max_len);
    if (n <= 0)
      return 0;
    return static_cast<size_t>(n);
  }

private:
  static speed_t to_speed_t(uint32_t baud) noexcept {
    switch (baud) {
      case 4800: return B4800;
      case 9600: return B9600;
      case 19200: return B19200;
      case 38400: return B38400;
#ifdef B57600
      case 57600: return B57600;
#endif
#ifdef B115200
      case 115200: return B115200;
#endif
#ifdef B230400
      case 230400: return B230400;
#endif
#ifdef B460800
      case 460800: return B460800;
#endif
      default: return B0; // неподдерживаемая скорость
    }
  }

  int fd_ = -1;
};

} // namespace light_nmea

#endif // __unix__ || __APPLE__