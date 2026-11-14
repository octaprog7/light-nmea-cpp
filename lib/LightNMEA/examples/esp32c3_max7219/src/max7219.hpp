// ============================================================================
// max7219.hpp - программное представление MAX7219 (драйвер регистров).
// MAX7219 управляется 2-байтовыми пакетами по SPI: [регистр, значение],
// CS активен LOW, порядок байтов big-endian (старший байт первым).
// ============================================================================
#pragma once

#include <Arduino.h>
#include <SPI.h>

class MAX7219 {
public:
    /// Адреса регистров MAX7219
    enum class Register : uint8_t {
        NOOP         = 0x00,  // нет операции
        DIGIT_0      = 0x01,  // знакоместо 0..7 (0x01..0x08)
        DECODE_MODE  = 0x09,  // вкл/выкл режима B-декодирования
        INTENSITY    = 0x0A,  // яркость (для всех знакомест)
        SCAN_LIMIT   = 0x0B,  // предел сканирования (1..8 цифр)
        SHUTDOWN     = 0x0C,  // вкл/выкл дисплея
        DISPLAY_TEST = 0x0F,  // режим проверки дисплея
    };

    explicit MAX7219(uint8_t cs_pin) noexcept : _cs(cs_pin) {}

    /// Аппаратная инициализация. Вызывать сразу после конструктора!
    void begin(SPIClass& bus, uint8_t columns = 8) {
        _spi = &bus;
        _columns = columns;
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);          // не выбран
        // DISPLAY_TEST сбрасываю первым
        setDisplayTest(false);            // обычный режим
        setShutdown(true);                // ВЫКлючаю дисплей
        setDecode(0x00);                  // отключаю B-код
        setScanLimit(_columns - 1);       // предел сканирования
        setShutdown(false);               // ВКлючаю дисплей
    }

    /// Отображает код символа code в знакоместе x (0..columns-1).
    void setChar(uint8_t code, uint8_t x) noexcept {
        sendCmd(digitRegister(x), code);
    }

    /// Устанавливает яркость всех знакомест, 0..15.
    void setBrightness(uint8_t value) noexcept {
        sendCmd(Register::INTENSITY, value & 0x0F);
    }

    /// Включает/отключает дисплей.
    void setShutdown(bool on) noexcept {
        sendCmd(Register::SHUTDOWN, on ? 0 : 1);
    }

    /// Режим проверки: все сегменты включены.
    void setDisplayTest(bool on) noexcept {
        sendCmd(Register::DISPLAY_TEST, on ? 1 : 0);
    }

    [[nodiscard]] uint8_t columns() const noexcept { return _columns; }

private:
    /// Регистр знакоместа x (0..7).
    [[nodiscard]] static Register digitRegister(uint8_t x) noexcept {
        return static_cast<Register>(static_cast<uint8_t>(Register::DIGIT_0) + x);
    }

    /// Режим B-декодирования по битам знакомест.
    void setDecode(uint8_t decode_bits) noexcept {
        sendCmd(Register::DECODE_MODE, decode_bits);
    }

    /// Количество сканируемых индикаторов, 1..8.
    void setScanLimit(uint8_t value) noexcept {
        sendCmd(Register::SCAN_LIMIT, value);
    }

    /// Пересылает пакет данных устройству по шине: [регистр, значение].
    /// Реализация transfer16() в arduino-esp32 копирует uint16_t в буфер
    /// и шлёт байты в порядке их расположения в памяти, поэтому на
    /// little-endian архитектуре младший байт уходит первым, а на AVR -
    /// старшим. Ради переносимости байты отправляю по одному!
    void sendCmd(Register reg, uint8_t value) noexcept {
        // Явные параметры: MODE0 (защёлкивание по фронту CLK), MSB first
        _spi->beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
        digitalWrite(_cs, LOW);                          // выбор чипа
        _spi->transfer(static_cast<uint8_t>(reg));       // регистр
        _spi->transfer(value);                           // значение
        digitalWrite(_cs, HIGH);                         // фиксация данных по фронту CS
        _spi->endTransaction();
    }

    SPIClass* _spi = nullptr;
    uint8_t _cs;
    uint8_t _columns = 0;
};
