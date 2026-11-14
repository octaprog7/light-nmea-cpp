// ============================================================================
// seg_display.hpp - символьный 7-сегментный дисплей на MAX7219.
//
// Расположение битов в коде символа (Table 6 даташита MAX7219):
//     D7=DP, D6=A, D5=B, D4=C, D3=D, D2=E, D1=F, D0=G
//
//       —A—
//     |     |
//     F     B
//     |     |
//       —G—
//     |     |
//     E     C
//     |     |
//       —D—
//             • DP
// ============================================================================
#pragma once

#include <Arduino.h>
#include "max7219.hpp"

/// Битовые маски сегментов
enum SegBits : uint8_t {
    SEG_G  = 1u << 0,
    SEG_F  = 1u << 1,
    SEG_E  = 1u << 2,
    SEG_D  = 1u << 3,
    SEG_C  = 1u << 4,
    SEG_B  = 1u << 5,
    SEG_A  = 1u << 6,
    SEG_DP = 1u << 7,
};

/// Погашенное знакоместо
inline constexpr uint8_t SEG_BLANK = 0x00;

/// Символ, который невозможно узнаваемо напечатать в знакоместе
inline constexpr uint8_t SEG_NON_PRINTABLE = SEG_A | SEG_D | SEG_G;

class SegDisplay {
public:
    explicit SegDisplay(MAX7219& controller) noexcept
        : _controller(controller) {}

    void init() noexcept {
        _partialUpdate = true;
        _inverseLogic = false;
        _reverseIndex = true;
        clear();
    }

    /// Выводит строку chars, начиная с позиции x.
    /// Символ '.' сливается с предыдущим символом (десятичная точка).
    void show(const char* chars, uint8_t x = 0) noexcept {
        const uint8_t cols = _controller.columns();
        // Индекс знакомества с учётом порядка: при reverse_index левому
        // символу строки соответствует старшее знакоместо
        uint8_t pos = _reverseIndex ? static_cast<uint8_t>(cols - 1 - x) : x;

        for (const char* p = chars; *p != '\0';) {
            const char c = *p++;
            const bool with_dp = (*p == '.');
            if (with_dp) ++p;
            if (pos >= cols) break;
            _controller.setChar(charToCode(c, with_dp), pos);
            if (_reverseIndex) {
                if (pos == 0) break;
                --pos;
            } else {
                ++pos;
            }
        }
    }

    /// Очищает весь дисплей.
    void clear() noexcept {
        for (uint8_t i = 0; i < _controller.columns(); ++i) {
            _controller.setChar(SEG_BLANK, i);
        }
    }

    void setPartialUpdate(bool value) noexcept { _partialUpdate = value; }
    void setInverseLogic(bool value) noexcept { _inverseLogic = value; }
    void setReverseIndex(bool value) noexcept { _reverseIndex = value; }

private:
    /// Преобразует символ (опционально с десятичной точкой) в код знакоместа.
    [[nodiscard]] uint8_t charToCode(char c, bool with_dp) const noexcept {
        uint8_t code = SEG_BLANK;
        switch (c) {
            case '0': code = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F; break;
            case '1': code = SEG_B | SEG_C; break;
            case '2': code = SEG_A | SEG_B | SEG_G | SEG_E | SEG_D; break;
            case '3': code = SEG_A | SEG_B | SEG_G | SEG_C | SEG_D; break;
            case '4': code = SEG_F | SEG_G | SEG_B | SEG_C; break;
            case '5': code = SEG_A | SEG_F | SEG_G | SEG_C | SEG_D; break;
            case '6': code = SEG_A | SEG_F | SEG_G | SEG_C | SEG_D | SEG_E; break;
            case '7': code = SEG_A | SEG_B | SEG_C; break;
            case '8': code = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G; break;
            case '9': code = SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G; break;
            // Буквы, читаемые однозначно
            case 'A': case 'a': code = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G; break;
            case 'b': code = SEG_C | SEG_D | SEG_E | SEG_F | SEG_G; break;
            case 'C': code = SEG_A | SEG_D | SEG_E | SEG_F; break;
            case 'c': code = SEG_D | SEG_E | SEG_G; break;
            case 'd': code = SEG_B | SEG_C | SEG_D | SEG_E | SEG_G; break;
            case 'E': code = SEG_A | SEG_G | SEG_D | SEG_E | SEG_F; break;
            case 'F': code = SEG_A | SEG_E | SEG_F | SEG_G; break;
            case 'G': code = SEG_A | SEG_C | SEG_D | SEG_E | SEG_F; break;
            case 'H': code = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G; break;
            case 'J': code = SEG_B | SEG_C | SEG_D; break;
            case 'L': code = SEG_D | SEG_E | SEG_F; break;
            case 'n': code = SEG_C | SEG_E | SEG_G; break;
            case 'o': code = SEG_C | SEG_D | SEG_E | SEG_G; break;
            case 'r': code = SEG_E | SEG_G; break;
            case 'P': code = SEG_A | SEG_B | SEG_E | SEG_F | SEG_G; break;
            case 'U': code = SEG_B | SEG_C | SEG_D | SEG_E | SEG_F; break;
            // Прочие символы
            case '-': code = SEG_G; break;
            case '_': code = SEG_D; break;
            case '=': code = SEG_G | SEG_D; break;
            case '|': code = SEG_E | SEG_F; break;
            case '[': code = SEG_D | SEG_E | SEG_F | SEG_A; break;
            case ']': code = SEG_A | SEG_B | SEG_C | SEG_D; break;
            case '\xB0':  // UTF-8 '°' - первый байт
                code = SEG_A | SEG_B | SEG_G | SEG_F; break;
            case '.': code = SEG_DP; break;
            case ' ': code = SEG_BLANK; break;
            default: code = SEG_NON_PRINTABLE; break;
        }
        if (with_dp) code |= SEG_DP;
        return _inverseLogic ? static_cast<uint8_t>(~code) : code;
    }

    MAX7219& _controller;
    bool _partialUpdate = true;
    bool _inverseLogic = false;
    bool _reverseIndex = true;
};
