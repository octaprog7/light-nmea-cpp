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
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// Сетевые потоки работают только на POSIX-системах (Linux/macOS/BSD).
#if defined(__unix__) || defined(__APPLE__)

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace light_nmea {

// ============================================================================
// TCPStream - поток из TCP-сокета (GNSS по локальной сети / gpsd / relay).
// ============================================================================
// Соединяется с host:port (host может быть IP-адресом или DNS-именем), переводит
// сокет в неблокирующий режим. read() возвращает 0, если данных нет.
//
//   TCPStream net;
//   if (net.connect("192.168.1.10", 2947)) {
//       reader.read_available(parser, net, get_time_ms);
//   }
class TCPStream final : public Stream {
public:
  TCPStream() noexcept = default;
  ~TCPStream() override { close(); }

  TCPStream(const TCPStream &) = delete;
  TCPStream &operator=(const TCPStream &) = delete;

  // Устанавливает TCP-соединение к host:port. Возвращает true при успехе.
  bool connect(const char *host, uint16_t port) noexcept {
    close();

    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u",
                  static_cast<unsigned>(port));

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;   // IPv4 или IPv6
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *res = nullptr;
    const int rc = ::getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0 || !res)
      return false;

    int fd = -1;
    for (addrinfo *it = res; it; it = it->ai_next) {
      fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
      if (fd < 0)
        continue;
      if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0)
        break;
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0)
      return false;

    set_nonblocking(fd);
    fd_ = fd;
    return true;
  }

  void close() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

  // Реализация абстрактного метода Stream::read().
  // Неблокирующая: возвращает 0, если данных в сокете сейчас нет.
  size_t read(uint8_t *buffer, size_t max_len) override {
    if (fd_ < 0)
      return 0;
    const ssize_t n = ::recv(fd_, buffer, max_len, 0);
    if (n <= 0) {
      if (n == 0)
        close(); // удалённый конец закрыл соединение
      else if (errno != EAGAIN && errno != EWOULDBLOCK)
        close(); // фатальная ошибка сокета
      return 0;
    }
    return static_cast<size_t>(n);
  }

private:
  static void set_nonblocking(int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
      ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }

  int fd_ = -1;
};

// ============================================================================
// WebSocketStream - минимальный WebSocket-клиент (RFC 6455) для POSIX.
// ============================================================================
// Позволяет читать NMEA-0183, передаваемый по WebSocket-серверу
// (например, сетевой мост для GNSS-приёмника). Реализация самодостаточна:
// HTTP-рукопожатие + SHA-1 + base64 встроены, внешние библиотеки не нужны.
//
//   WebSocketStream ws;
//   if (ws.connect("192.168.1.10", 8080, "/nmea")) {
//       reader.read_available(parser, ws, get_time_ms);
//   }
//
// Принимает текстовые (0x1) и бинарные (0x2) фреймы, поддерживает
// фрагментацию (0x0), отвечает на ping (0x9) фреймом pong и корректно
// обрабатывает close (0x8).
class WebSocketStream final : public Stream {
public:
  WebSocketStream() noexcept = default;
  ~WebSocketStream() override { close(); }

  WebSocketStream(const WebSocketStream &) = delete;
  WebSocketStream &operator=(const WebSocketStream &) = delete;

  // Устанавливает TCP-соединение и выполняет HTTP-рукопожатие WebSocket.
  bool connect(const char *host, uint16_t port,
               const char *path = "/") noexcept {
    close();
    host_ = host ? host : "";
    path_ = (path && *path) ? path : "/";
    port_ = port;

    if (!tcp_connect(host_.c_str(), port_))
      return false;

    if (!handshake()) {
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
    rx_.clear();
    rx_pos_ = 0;
    data_.clear();
    data_pos_ = 0;
    frag_.clear();
    fstate_ = FrameState::Header1;
    opcode_ = 0;
    fin_ = false;
    payload_len_ = 0;
    in_fragment_ = false;
  }

  [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

  // Неблокирующее чтение полезной нагрузки текстовых/бинарных фреймов.
  size_t read(uint8_t *buffer, size_t max_len) override {
    if (fd_ < 0)
      return 0;

    // 1) Сначала отдаём уже распарсенные данные.
    if (data_pos_ < data_.size()) {
      const size_t avail = data_.size() - data_pos_;
      const size_t n = (avail < max_len) ? avail : max_len;
      std::memcpy(buffer, data_.data() + data_pos_, n);
      data_pos_ += n;
      if (data_pos_ == data_.size()) {
        data_.clear();
        data_pos_ = 0;
      }
      return n;
    }

    // 2) Подкачивает кадры/frames, пока не появится полезная нагрузка.
    while (pump_once()) {
      if (data_pos_ < data_.size())
        break;
    }

    if (data_pos_ < data_.size()) {
      const size_t avail = data_.size() - data_pos_;
      const size_t n = (avail < max_len) ? avail : max_len;
      std::memcpy(buffer, data_.data() + data_pos_, n);
      data_pos_ += n;
      if (data_pos_ == data_.size()) {
        data_.clear();
        data_pos_ = 0;
      }
      return n;
    }
    return 0;
  }

private:
  // --------------------------------------------------------------------------
  // Низкоуровневый TCP
  // --------------------------------------------------------------------------
  bool tcp_connect(const char *host, uint16_t port) noexcept {
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u",
                  static_cast<unsigned>(port));

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *res = nullptr;
    if (::getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
      return false;

    int fd = -1;
    for (addrinfo *it = res; it; it = it->ai_next) {
      fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
      if (fd < 0)
        continue;
      if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0)
        break;
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0)
      return false;

    fd_ = fd;
    return true;
  }

  ssize_t send_all(const uint8_t *data, size_t len) noexcept {
    size_t sent = 0;
    while (sent < len) {
      const ssize_t n = ::send(fd_, data + sent, len - sent, 0);
      if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          // Сокет блокирующий на этапе рукопожатия - такого быть не должно.
          continue;
        }
        return -1;
      }
      sent += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(sent);
  }

  // --------------------------------------------------------------------------
  // Handshake (HTTP Upgrade)
  // --------------------------------------------------------------------------
  bool handshake() noexcept {
    // Таймаут приёма ответа - 5 секунд (после переведём сокет в non-blocking).
    timeval tv{5, 0};
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Ключ: 16 случайных байт -> base64.
    uint8_t key_bytes[16];
    random_bytes(key_bytes, sizeof(key_bytes));
    const std::string key = base64_encode(key_bytes, sizeof(key_bytes));

    std::string req = "GET " + path_ + " HTTP/1.1\r\n";
    req += "Host: " + host_ + ":" + std::to_string(port_) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n\r\n";

    if (send_all(reinterpret_cast<const uint8_t *>(req.data()), req.size()) < 0)
      return false;

    // Чтение ответа до символов конца пакета ("\r\n\r\n").
    std::string resp;
    char tmp[512];
    while (resp.find("\r\n\r\n") == std::string::npos) {
      const ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
      if (n <= 0)
        return false;
      resp.append(tmp, static_cast<size_t>(n));
      if (resp.size() > 16384)
        return false; // защита буфера от разрастания
    }

    // Статус должен быть равен: 101 Switching Protocols.
    if (resp.compare(0, 12, "HTTP/1.1 101") != 0 &&
        resp.compare(0, 12, "HTTP/1.0 101") != 0)
      return false;

    // Ожидаемое значение Sec-WebSocket-Accept = base64(SHA1(key+GUID)).
    const std::vector<uint8_t> digest =
        sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    const std::string accept_expected =
        base64_encode(digest.data(), digest.size());

    // Поиск в ответе заголовка Sec-WebSocket-Accept.
    const std::string lower = to_lower(resp);
    const std::string marker = "sec-websocket-accept:";
    const size_t pos = lower.find(marker);
    if (pos == std::string::npos)
      return false;
    const size_t vstart = pos + marker.size();
    const size_t vend = resp.find("\r\n", vstart);
    const std::string value =
        trim(resp.substr(vstart, vend == std::string::npos
                                     ? std::string::npos
                                     : vend - vstart));
    if (!icase_equal(value, accept_expected))
      return false;

    // Часть данных в буфере после заголовков - начало фреймов.
    const size_t header_end = resp.find("\r\n\r\n") + 4;
    if (header_end < resp.size()) {
      rx_.assign(reinterpret_cast<const uint8_t *>(resp.data()) + header_end,
                 reinterpret_cast<const uint8_t *>(resp.data()) + resp.size());
      rx_pos_ = 0;
    }

    // Переключает сокет в неблокирующий режим.
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags >= 0)
      ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    return true;
  }

  // --------------------------------------------------------------------------
  // Приём и разбор фреймов
  // --------------------------------------------------------------------------
  // Одна попытка recv() + разбор всех доступных фреймов.
  // Возвращает true, если байты получены по сети (даже если полезной нагрузки пока нет),
  // false - данных нет (EAGAIN/EOF/ошибка).
  bool pump_once() noexcept {
    uint8_t buf[2048];
    const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n <= 0) {
      if (n == 0) {
        close(); // соединение закрыто сервером
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        close();
      }
      return false;
    }

    rx_.insert(rx_.end(), buf, buf + n);

    // Сжатие размера буфера, чтобы не разрастался.
    if (rx_pos_ > 0 && rx_pos_ >= rx_.size() / 2) {
      rx_.erase(rx_.begin(), rx_.begin() + static_cast<ptrdiff_t>(rx_pos_));
      rx_pos_ = 0;
    }
    // Защита от разрастания (мусор без валидных фреймов).
    if (rx_.size() > 1u << 20) {
      close();
      return false;
    }

    while (true) {
      const int rc = try_parse_frame();
      if (rc == 0)
        break; // нужно больше данных
      if (rc < 0) {
        close();
        return false;
      }
    }
    return true;
  }

  // Разбирает один фрейм из rx_. Возвращает 1 (фрейм обработан),
  // 0 (нужно больше данных), -1 (ошибка протокола).
  int try_parse_frame() noexcept {
    const size_t avail = rx_.size() - rx_pos_;

    if (fstate_ == FrameState::Header1) {
      if (avail < 2)
        return 0;
      const uint8_t b0 = rx_[rx_pos_];
      const uint8_t b1 = rx_[rx_pos_ + 1];
      fin_ = (b0 & 0x80) != 0;
      opcode_ = b0 & 0x0F;
      if ((b1 & 0x80) != 0)
        return -1; // сервер НЕ должен маскировать фреймы
      payload_len_ = b1 & 0x7F;
      rx_pos_ += 2;
      if (payload_len_ == 126)
        fstate_ = FrameState::ExtLen16;
      else if (payload_len_ == 127)
        fstate_ = FrameState::ExtLen64;
      else
        fstate_ = FrameState::Payload;
    }

    if (fstate_ == FrameState::ExtLen16) {
      if (rx_.size() - rx_pos_ < 2)
        return 0;
      payload_len_ = (static_cast<uint64_t>(rx_[rx_pos_]) << 8) |
                     rx_[rx_pos_ + 1];
      rx_pos_ += 2;
      fstate_ = FrameState::Payload;
    } else if (fstate_ == FrameState::ExtLen64) {
      if (rx_.size() - rx_pos_ < 8)
        return 0;
      payload_len_ = 0;
      for (int i = 0; i < 8; ++i)
        payload_len_ = (payload_len_ << 8) | rx_[rx_pos_ + i];
      rx_pos_ += 8;
      fstate_ = FrameState::Payload;
    }

    if (fstate_ == FrameState::Payload) {
      if (payload_len_ > (1u << 20))
        return -1; // слишком большой фрейм
      if (rx_.size() - rx_pos_ < payload_len_)
        return 0;

      const uint8_t *payload = rx_.data() + rx_pos_;
      const size_t len = static_cast<size_t>(payload_len_);

      switch (opcode_) {
      case 0x0: // продолжение
        frag_.insert(frag_.end(), payload, payload + len);
        if (fin_) {
          if (in_fragment_) {
            append_data(frag_);
            frag_.clear();
            in_fragment_ = false;
          } else {
            return -1; // продолжение без начального фрагмента
          }
        }
        break;
      case 0x1: // text
      case 0x2: // binary
        if (fin_) {
          append_payload(payload, len);
        } else {
          frag_.assign(payload, payload + len);
          in_fragment_ = true;
        }
        break;
      case 0x8: // close
        reply_close(payload, len);
        return -1; // завершение чтения
      case 0x9:   // ping
        reply_pong(payload, len);
        break;
      case 0xA: // pong
        break;
      default:
        return -1; // неизвестный код операции
      }

      rx_pos_ += payload_len_;
      fstate_ = FrameState::Header1;
      opcode_ = 0;
      payload_len_ = 0;
      return 1;
    }
    return 0;
  }

  void append_payload(const uint8_t *p, size_t len) noexcept {
    if (data_.size() + len > (1u << 20))
      return; // защита очереди от разрастания
    data_.insert(data_.end(), p, p + len);
  }

  void append_data(const std::vector<uint8_t> &chunk) noexcept {
    if (data_.size() + chunk.size() > (1u << 20))
      return;
    data_.insert(data_.end(), chunk.begin(), chunk.end());
  }

  bool reply_pong(const uint8_t *payload, size_t len) noexcept {
    if (fd_ < 0 || len > 125)
      return false;
    uint8_t frame[2 + 125];
    frame[0] = 0x8A; // FIN + opcode pong
    frame[1] = static_cast<uint8_t>(len);
    std::memcpy(frame + 2, payload, len);
    return send_all(frame, 2 + len) > 0;
  }

  void reply_close(const uint8_t *payload, size_t len) noexcept {
    uint8_t frame[2 + 125];
    frame[0] = 0x88; // FIN + opcode close
    frame[1] = static_cast<uint8_t>(len < 125 ? len : 125);
    if (len > 0)
      std::memcpy(frame + 2, payload, frame[1]);
    send_all(frame, 2 + frame[1]);
  }

  // --------------------------------------------------------------------------
  // SHA-1 (для Sec-WebSocket-Accept)
  // --------------------------------------------------------------------------
  static std::vector<uint8_t> sha1(const std::string &msg) noexcept {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476,
             h4 = 0xC3D2E1F0;
    const uint64_t bitlen = static_cast<uint64_t>(msg.size()) * 8;

    std::vector<uint8_t> m(msg.begin(), msg.end());
    m.push_back(0x80);
    while (m.size() % 64 != 56)
      m.push_back(0x00);
    for (int i = 7; i >= 0; --i)
      m.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xFF));

    auto rol = [](uint32_t v, int b) {
      return (v << b) | (v >> (32 - b));
    };

    for (size_t off = 0; off < m.size(); off += 64) {
      uint32_t w[80];
      for (int i = 0; i < 16; ++i)
        w[i] = (static_cast<uint32_t>(m[off + i * 4]) << 24) |
               (static_cast<uint32_t>(m[off + i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(m[off + i * 4 + 2]) << 8) |
               static_cast<uint32_t>(m[off + i * 4 + 3]);
      for (int i = 16; i < 80; ++i)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

      uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
      for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
          f = (b & c) | ((~b) & d);
          k = 0x5A827999;
        } else if (i < 40) {
          f = b ^ c ^ d;
          k = 0x6ED9EBA1;
        } else if (i < 60) {
          f = (b & c) | (b & d) | (c & d);
          k = 0x8F1BBCDC;
        } else {
          f = b ^ c ^ d;
          k = 0xCA62C1D6;
        }
        const uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
      }
      h0 += a;
      h1 += b;
      h2 += c;
      h3 += d;
      h4 += e;
    }

    std::vector<uint8_t> out(20);
    const uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
      out[i * 4] = static_cast<uint8_t>(hs[i] >> 24);
      out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
      out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
      out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
    }
    return out;
  }

  // --------------------------------------------------------------------------
  // Base64
  // --------------------------------------------------------------------------
  static std::string base64_encode(const uint8_t *data, size_t len) noexcept {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
      const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                         ((i + 1 < len ? data[i + 1] : 0) << 8) |
                         (i + 2 < len ? data[i + 2] : 0);
      out += tbl[(n >> 18) & 63];
      out += tbl[(n >> 12) & 63];
      out += (i + 1 < len) ? tbl[(n >> 6) & 63] : '=';
      out += (i + 2 < len) ? tbl[n & 63] : '=';
    }
    return out;
  }

  // --------------------------------------------------------------------------
  // Случайные байты для Sec-WebSocket-Key
  // --------------------------------------------------------------------------
  static void random_bytes(uint8_t *out, size_t len) noexcept {
    FILE *f = std::fopen("/dev/urandom", "rb");
    if (f) {
      const size_t got = std::fread(out, 1, len, f);
      std::fclose(f);
      if (got == len)
        return;
    }
    // FallBack: смесь времени и счётчика (не критично для ключа).
    uint64_t seed = static_cast<uint64_t>(::time(nullptr)) ^
                    static_cast<uint64_t>(::getpid()) << 32;
    for (size_t i = 0; i < len; ++i) {
      seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
      out[i] = static_cast<uint8_t>(seed >> 33);
    }
  }

  // --------------------------------------------------------------------------
  // Строковые утилиты
  // --------------------------------------------------------------------------
  static std::string trim(const std::string &s) noexcept {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
      ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                     s[b - 1] == '\n'))
      --b;
    return s.substr(a, b - a);
  }

  static std::string to_lower(const std::string &s) noexcept {
    std::string r = s;
    for (char &c : r) {
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    }
    return r;
  }

  static bool icase_equal(const std::string &a, const std::string &b) noexcept {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      char x = a[i], y = b[i];
      if (x >= 'A' && x <= 'Z')
        x = static_cast<char>(x - 'A' + 'a');
      if (y >= 'A' && y <= 'Z')
        y = static_cast<char>(y - 'A' + 'a');
      if (x != y)
        return false;
    }
    return true;
  }

  // --------------------------------------------------------------------------
  // Состояние
  // --------------------------------------------------------------------------
  enum class FrameState : uint8_t { Header1, ExtLen16, ExtLen64, Payload };

  int fd_ = -1;
  std::string host_;
  std::string path_;
  uint16_t port_ = 0;

  std::vector<uint8_t> rx_; // входной буфер сокета
  size_t rx_pos_ = 0;
  std::vector<uint8_t> data_; // очередь полезной нагрузки для read()
  size_t data_pos_ = 0;

  FrameState fstate_ = FrameState::Header1;
  uint8_t opcode_ = 0;
  bool fin_ = false;
  uint64_t payload_len_ = 0;

  std::vector<uint8_t> frag_; // фрагмент (для фреймов-продолжений)
  bool in_fragment_ = false;
};

} // namespace light_nmea

#endif // __unix__ || __APPLE__