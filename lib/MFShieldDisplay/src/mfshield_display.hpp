/*
 * MFShieldDisplay - 4-digit 7-segment display driver for the Arduino UNO
 * Multi-Function Shield (2 x 74HC595).
 * Copyright (C) 2026 Roman Shevchik
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// ============================================================================
// mfshield_display.hpp - 4-разрядный семисегментный индикатор шилда
// "Multi-Function Shield" для Arduino UNO R3 (2 x 74HC595).
//
// Поддерживаемые символы:
//     0-9, A, C, E, F, H, L, P, U, '-', '_', ' ', '.'
//     (строчные a, c, e, f, h, l, p, u дают те же значки)
// Неизвестный символ выводится как пустое знакоместо (пробел).
//
// Аппаратная часть (классическая ревизия шилда):
//     DATA  = D8  (74HC595 DS,   вывод 14)
//     CLOCK = D7  (74HC595 SHCP, вывод 11)
//     LATCH = D4  (74HC595 STCP, вывод 12)
//
// Два регистра 74HC595 включены каскадом. За одну посылку выдвигаются
// два байта:
//     1-й байт - сегменты: 1 = сегмент ПОГАШЕН (общий анод, активный LOW),
//                бит 7 = десятичная точка (сбросить бит, чтобы зажечь);
//     2-й байт - выбор разряда: one-hot в младшем полубайте,
//                старший полубайт = 1 (0xF1, 0xF2, 0xF4, 0xF8).
//
// Расположение битов байта сегментов (классическая ревизия):
//     bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G, bit7=DP
//
//       —A—
//     |     |
//     F     B
//     |     |
//       —G—
//     |     |
//     E     C
//     |     |
//       —D—        * DP
//
// Индикация динамическая: в каждый момент горит только один разряд.
// Класс НЕ использует таймеры и прерывания (в отличие от библиотеки
// MultiFuncShield на Timer0) - достаточно вызывать refresh() в loop().
// Это совместимо с SoftwareSerial, tone(), Servo и т.п.
//
// Некоторые клоны шилда имеют инвертированную логику (сегменты активны HIGH,
// выбор разряда активен LOW). Для них: setWiring(Wiring::InvertedClone).
// ============================================================================
#pragma once

#include <Arduino.h>

class MFShieldDisplay {
public:
    static constexpr uint8_t DIGIT_COUNT = 4;

    static constexpr uint8_t DEFAULT_LATCH_PIN = 4;  // D4
    static constexpr uint8_t DEFAULT_CLOCK_PIN = 7;  // D7
    static constexpr uint8_t DEFAULT_DATA_PIN  = 8;  // D8

    /// Полярность шилда.
    enum class Wiring : uint8_t {
        Classic = 0,        // сегменты активны LOW, выбор разряда активен HIGH
        InvertedClone = 1,  // сегменты активны HIGH, выбор разряда активен LOW
    };

    /// Логические маски сегментов (активный уровень - 1).
    enum SegMask : uint8_t {
        SEG_A  = 1u << 0,
        SEG_B  = 1u << 1,
        SEG_C  = 1u << 2,
        SEG_D  = 1u << 3,
        SEG_E  = 1u << 4,
        SEG_F  = 1u << 5,
        SEG_G  = 1u << 6,
        SEG_DP = 1u << 7,
    };

    explicit MFShieldDisplay(uint8_t latchPin = DEFAULT_LATCH_PIN,
                             uint8_t clockPin = DEFAULT_CLOCK_PIN,
                             uint8_t dataPin  = DEFAULT_DATA_PIN) noexcept
        : _latchPin(latchPin), _clockPin(clockPin), _dataPin(dataPin) {}

    // ------------------------------------------------------------------
    // Инициализация и настройка
    // ------------------------------------------------------------------

    /// Настраивает пины и гасит индикатор. Вызывать в setup().
    void begin() noexcept {
        pinMode(_latchPin, OUTPUT);
        pinMode(_clockPin, OUTPUT);
        pinMode(_dataPin, OUTPUT);
        digitalWrite(_latchPin, LOW);
        digitalWrite(_clockPin, LOW);
        digitalWrite(_dataPin, LOW);

        clear();
        setBrightness(3);

        _activeDigit = 0;
        _blanking = false;
        writeRaw(_activeDigit, _seg[logicalIndex(_activeDigit)]);
        _phaseStartUs = micros();
    }

    /// Выбор полярности конкретной ревизии шилда.
    void setWiring(Wiring wiring) noexcept { _wiring = wiring; }

    /// Зеркальный порядок знакомест (если строка выводится справа налево).
    void setReverseDigits(bool reverse) noexcept { _reverse = reverse; }

    /// Яркость: 0 = минимум, 3 = максимум (по умолчанию 3).
    void setBrightness(uint8_t level) noexcept {
        if (level > 3) level = 3;
        _brightness = level;
        static const uint16_t kBlankUs[4] = {2400, 1600, 800, 0};
        _blankTimeUs = kBlankUs[level];
    }

    // ------------------------------------------------------------------
    // Вывод содержимого
    // ------------------------------------------------------------------

    /// Гасит все знакоместа.
    void clear() noexcept {
        for (uint8_t i = 0; i < DIGIT_COUNT; ++i) _seg[i] = 0;
    }

    /// Устанавливает один символ в позицию pos (0 = левое знакоместо).
    /// '.' включает десятичную точку в этой позиции.
    void setChar(uint8_t pos, char c) noexcept {
        if (pos >= DIGIT_COUNT) return;
        if (c == '.') {
            setDecimalPoint(pos, true);
        } else {
            _seg[pos] = glyph(c);
        }
    }

    /// Включает/выключает десятичную точку в позиции pos.
    void setDecimalPoint(uint8_t pos, bool on = true) noexcept {
        if (pos >= DIGIT_COUNT) return;
        if (on) {
            _seg[pos] = static_cast<uint8_t>(_seg[pos] | SEG_DP);
        } else {
            _seg[pos] = static_cast<uint8_t>(_seg[pos] & ~SEG_DP);
        }
    }

    /// Выводит строку с выравниванием по левому краю.
    /// '.' сливается с предыдущим символом (десятичная точка).
    void print(const char* text) noexcept { render(text, false); }

    /// Выводит строку с выравниванием по правому краю.
    void printRight(const char* text) noexcept { render(text, true); }

    // ------------------------------------------------------------------
    // Динамическая индикация
    // ------------------------------------------------------------------

    /// Переключает разряды. Вызывать как можно чаще, например в loop().
    void refresh() noexcept {
        const uint32_t now = micros();

        if (_blanking) {
            if (static_cast<uint32_t>(now - _phaseStartUs) < _blankTimeUs) return;
            _blanking = false;
            nextDigit();
            return;
        }

        if (static_cast<uint32_t>(now - _phaseStartUs) < ON_TIME_US) return;

        // Пауза гашения для регулировки яркости.
        if (_blankTimeUs != 0) {
            writeRaw(_activeDigit, 0);
            _blanking = true;
            _phaseStartUs = now;
            return;
        }

        nextDigit();
    }

private:
    static constexpr uint16_t ON_TIME_US = 1200;

    /// Логический индекс знакоместа (с учётом зеркального порядка).
    uint8_t logicalIndex(uint8_t physical) const noexcept {
        return _reverse ? static_cast<uint8_t>(DIGIT_COUNT - 1 - physical) : physical;
    }

    /// Зажигает следующий разряд.
    void nextDigit() noexcept {
        _activeDigit = static_cast<uint8_t>((_activeDigit + 1) % DIGIT_COUNT);
        writeRaw(_activeDigit, _seg[logicalIndex(_activeDigit)]);
        _phaseStartUs = micros();
    }

    /// Выдвигает в 74HC595 байт сегментов и байт выбора разряда.
    void writeRaw(uint8_t digit, uint8_t mask) noexcept {
        uint8_t segments;
        uint8_t select;

        if (_wiring == Wiring::InvertedClone) {
            // Клон: сегменты активны HIGH, выбор разряда активен LOW.
            segments = mask;
            select = static_cast<uint8_t>(0xF0 | ((~(1u << digit)) & 0x0F));
        } else {
            // Классический шилд: сегменты активны LOW, выбор разряда HIGH.
            segments = static_cast<uint8_t>(~mask);
            select = static_cast<uint8_t>(0xF0 | (1u << digit));
        }

        digitalWrite(_latchPin, LOW);
        shiftOut(_dataPin, _clockPin, MSBFIRST, segments);
        shiftOut(_dataPin, _clockPin, MSBFIRST, select);
        digitalWrite(_latchPin, HIGH);
    }

    /// Раскладывает строку по знакоместам.
    void render(const char* text, bool rightJustify) noexcept {
        uint8_t tmp[DIGIT_COUNT] = {0, 0, 0, 0};
        uint8_t count = 0;

        if (text != nullptr) {
            for (const char* p = text; *p != '\0' && count < DIGIT_COUNT; ++p) {
                if (*p == '.') {
                    if (count > 0) {
                        tmp[count - 1] = static_cast<uint8_t>(tmp[count - 1] | SEG_DP);
                    } else {
                        tmp[count++] = SEG_DP;
                    }
                } else {
                    tmp[count++] = glyph(*p);
                }
            }
        }

        if (rightJustify && count < DIGIT_COUNT) {
            const uint8_t offset = static_cast<uint8_t>(DIGIT_COUNT - count);
            for (uint8_t i = 0; i < DIGIT_COUNT; ++i) _seg[i] = 0;
            for (uint8_t i = 0; i < count; ++i) _seg[offset + i] = tmp[i];
        } else {
            for (uint8_t i = 0; i < DIGIT_COUNT; ++i) _seg[i] = tmp[i];
        }
    }

    /// Преобразует символ в логическую маску сегментов.
    /// Неизвестный символ -> 0 (пустое знакоместо).
    static uint8_t glyph(char c) noexcept {
        switch (c) {
            case '0': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
            case '1': return SEG_B | SEG_C;
            case '2': return SEG_A | SEG_B | SEG_D | SEG_E | SEG_G;
            case '3': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_G;
            case '4': return SEG_B | SEG_C | SEG_F | SEG_G;
            case '5': return SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;
            case '6': return SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
            case '7': return SEG_A | SEG_B | SEG_C;
            case '8': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
            case '9': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G;

            case 'A': case 'a': return SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
            case 'C': case 'c': return SEG_A | SEG_D | SEG_E | SEG_F;
            case 'E': case 'e': return SEG_A | SEG_D | SEG_E | SEG_F | SEG_G;
            case 'F': case 'f': return SEG_A | SEG_E | SEG_F | SEG_G;
            case 'H': case 'h': return SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
            case 'L': case 'l': return SEG_D | SEG_E | SEG_F;
            case 'P': case 'p': return SEG_A | SEG_B | SEG_E | SEG_F | SEG_G;
            case 'U': case 'u': return SEG_C | SEG_D | SEG_E;

            case '-': return SEG_G;
            case '_': return SEG_D;
            case ' ': return 0;
            case '.': return SEG_DP;
            default:  return 0;
        }
    }

    uint8_t _latchPin;
    uint8_t _clockPin;
    uint8_t _dataPin;

    Wiring  _wiring = Wiring::Classic;
    bool    _reverse = false;

    uint8_t _seg[DIGIT_COUNT] = {0, 0, 0, 0};

    uint8_t  _activeDigit = 0;
    uint32_t _phaseStartUs = 0;
    bool     _blanking = false;

    uint8_t  _brightness = 3;
    uint16_t _blankTimeUs = 0;
};
