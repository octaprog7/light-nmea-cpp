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

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace light_nmea {

// ============================================================================
// Абстрактный поток байтов.
// ============================================================================
// Любой источник NMEA-данных (UART, сокет, файл, кольцевой буфер и т.д.)
// реализует этот интерфейс. Это позволяет NMEAStreamReader читать данные из
// любого источника единообразно через виртуальную функцию read().
class Stream {
public:
  virtual ~Stream() = default;

  // Читает до max_len байт в buffer.
  // Возвращает количество РЕАЛЬНО прочитанных байт.
  // Возврат 0 означает "данных нет" (неблокирующий источник) или конец
  // потока (EOF). NMEAStreamReader завершает цикл чтения при 0.
  virtual size_t read(uint8_t *buffer, size_t max_len) = 0;

  // Необязательный хук: сбросить внутреннее состояние потока
  // (например, перемотать файл на начало). По умолчанию - no-op.
  virtual void reset_stream() noexcept {}

protected:
  Stream() = default;
};

// ============================================================================
// Реализация потока поверх обычного файла (FILE*).
// ============================================================================
// Позволяет повторно проигрывать записанные NMEA-треки (*.nmea) из файла.
class FileStream final : public Stream {
public:
  FileStream() noexcept = default;

  // Открывает файл сразу в конструкторе. Проверяйте is_open().
  explicit FileStream(const char *path) noexcept { open(path); }

  ~FileStream() override { close(); }

  // Запрет копирования (владеет FILE*).
  FileStream(const FileStream &) = delete;
  FileStream &operator=(const FileStream &) = delete;

  // Открывает файл в бинарном режиме для чтения.
  // Возвращает true при успехе. Закрывает предыдущий файл, если он был открыт.
  bool open(const char *path) noexcept {
    close();
    file_ = std::fopen(path, "rb");
    return file_ != nullptr;
  }

  [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }

  void close() noexcept {
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

  [[nodiscard]] bool eof() const noexcept {
    return file_ != nullptr && std::feof(file_) != 0;
  }

  size_t read(uint8_t *buffer, size_t max_len) override {
    if (!file_)
      return 0;
    return std::fread(buffer, 1, max_len, file_);
  }

  void reset_stream() noexcept override {
    if (file_)
      std::rewind(file_);
  }

private:
  FILE *file_ = nullptr;
};

} // namespace light_nmea