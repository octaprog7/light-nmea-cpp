/*
 * LightNMEA - Ultra-fast NMEA GPS parser
 *
 * Пример чтения NMEA-данных из СЕТЕВОГО устройства (TCP-сокет).
 *
 * Показывает, как реализовать собственный потомок абстрактного класса
 * light_nmea::Stream для подключения к GNSS-приёмнику или серверу по TCP
 * (например, gpsd, сервер на лодке и т.д.).
 *
 * Требуется POSIX-сеть (Linux/macOS/BSD). Для Windows используйте Winsock.
 */

#include "nmea_stream_reader.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <ctime>
#include <iostream>

using namespace light_nmea;

// Функция получения текущего времени в мс (для анти-спам фильтра).
uint32_t get_time_ms() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ============================================================================
// Сетевой поток: наследник абстрактного Stream.
// ============================================================================
class TCPStream final : public Stream {
public:
  TCPStream() noexcept = default;
  ~TCPStream() override { close(); }

  TCPStream(const TCPStream &) = delete;
  TCPStream &operator=(const TCPStream &) = delete;

  // Устанавливает TCP-соединение к host:port.
  // Возвращает true при успехе.
  bool connect(const char *host, uint16_t port) noexcept {
    close();

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0)
      return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
      close();
      return false;
    }

    if (::connect(fd_, reinterpret_cast<const sockaddr *>(&addr),
                  sizeof(addr)) != 0) {
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

  [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

  // Реализация абстрактного метода Stream::read().
  // Для неблокирующего источника возврат 0 означает "данных нет".
  size_t read(uint8_t *buffer, size_t max_len) override {
    if (fd_ < 0)
      return 0;
    ssize_t n = ::recv(fd_, buffer, max_len, 0);
    if (n <= 0)
      return 0; // закрытие соединения или ошибка -> конец потока
    return static_cast<size_t>(n);
  }

private:
  int fd_ = -1;
};

// Callback статистики (std::endl сбрасывает буфер для вывода в реальном времени)
void on_packet(bool recognized, bool valid, Constellation cst) {
  std::cout << "recognized=" << recognized << " valid=" << valid
            << " cst=" << static_cast<int>(cst) << std::endl;
}

int main(int argc, char **argv) {
  const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
  const uint16_t port = (argc > 2) ? static_cast<uint16_t>(atoi(argv[2])) : 2947;

  TCPStream net;
  if (!net.connect(host, port)) {
    std::cerr << "Failed to connect to " << host << ":" << port << "\n";
    return 1;
  }
  std::cout << "Connected to " << host << ":" << port << "\n";

  Parser parser;
  NMEAStreamReader reader;

  // Анти-спам фильтр: не чаще 1 GGA/RMC в 200 мс.
  reader.set_anti_spam_interval(200);

  // Читает пакеты, пока соединение открыто.
  while (net.is_open()) {
    reader.read_available(parser, net, get_time_ms, on_packet);
  }

  auto stats = reader.get_stats();
  std::cout << "Total processed: " << stats.packets_processed
            << ", aborted: " << stats.packets_aborted << std::endl;
  return 0;
}