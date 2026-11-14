// ============================================================================
// main.cpp - проверочное приложение для MFShieldDisplay.
//
// Плата: Arduino UNO R3 (ATmega328P) + Multi-Function Shield.
// Индикатор: 4 разряда, 2 x 74HC595, LATCH=D4, CLOCK=D7, DATA=D8.
//
// Автоматические фазы (повторяются по кругу):
//   1) "8888" - проверка всех сегментов;
//   2) каждый поддерживаемый символ во всех 4 знакоместах;
//   3) "1.2.3.4" - все десятичные точки;
//   4) строки "0123", "4567", "89AC", "EFHL", "PU-.";
//   5) ступени яркости 3 -> 2 -> 1 -> 0.
//
// Кнопки шилда:
//   S1 (A1) - полярность: Classic <-> InvertedClone;
//   S2 (A2) - порядок знакомест: прямой <-> зеркальный;
//   S3 (A3) - яркость 0..3 вручную.
//
// Если символы "мусорные" или индикатор тёмный - нажмите S1.
// Если строка выводится справа налево - нажмите S2.
// ============================================================================

#include <Arduino.h>

#include "mfshield_display.hpp"

namespace {

constexpr uint8_t PIN_LATCH = 4;  // D4
constexpr uint8_t PIN_CLOCK = 7;  // D7
constexpr uint8_t PIN_DATA  = 8;  // D8

constexpr uint8_t PIN_BTN_S1 = A1;
constexpr uint8_t PIN_BTN_S2 = A2;
constexpr uint8_t PIN_BTN_S3 = A3;

constexpr uint32_t ALL_SEG_MS    = 1000;
constexpr uint32_t GLYPH_MS      = 600;
constexpr uint32_t DOTS_MS       = 1500;
constexpr uint32_t STRING_MS     = 800;
constexpr uint32_t BRIGHTNESS_MS = 800;
constexpr uint32_t DEBOUNCE_MS   = 30;

/// Все поддерживаемые одиночные символы ('.' проверяется отдельной фазой).
constexpr char GLYPHS[] = "0123456789ACEFHLPU-_";

/// Наборы по 4 символа, включая десятичную точку.
constexpr const char* const STRINGS[] = {
    "0123", "4567", "89AC", "EFHL", "PU-."
};
constexpr uint8_t STRING_COUNT =
    static_cast<uint8_t>(sizeof(STRINGS) / sizeof(STRINGS[0]));

MFShieldDisplay display(PIN_LATCH, PIN_CLOCK, PIN_DATA);

enum class Phase : uint8_t {
    AllSegments,
    Glyphs,
    Dots,
    Strings,
    Brightness,
};

Phase phase = Phase::AllSegments;
uint32_t phaseStartMs = 0;
uint8_t glyphIndex = 0;
uint8_t stringIndex = 0;
uint8_t brightness = 3;

bool wiringInverted = false;
bool digitsReversed = false;

struct Button {
    uint8_t pin;
    bool stable = false;
    bool lastReading = false;
    uint32_t lastChangeMs = 0;
};

Button buttons[3] = {{PIN_BTN_S1}, {PIN_BTN_S2}, {PIN_BTN_S3}};

/// Возвращает true один раз на фронт нажатия (active LOW + антидребезг).
bool pressed(Button& button) {
    const bool reading = (digitalRead(button.pin) == LOW);
    const uint32_t now = millis();

    if (reading != button.lastReading) {
        button.lastReading = reading;
        button.lastChangeMs = now;
    }

    if ((now - button.lastChangeMs) >= DEBOUNCE_MS && reading != button.stable) {
        button.stable = reading;
        return button.stable;
    }
    return false;
}

/// Показывает один символ во всех четырёх знакоместах.
void showGlyph(char c) {
    char text[MFShieldDisplay::DIGIT_COUNT + 1];
    for (uint8_t i = 0; i < MFShieldDisplay::DIGIT_COUNT; ++i) {
        text[i] = c;
    }
    text[MFShieldDisplay::DIGIT_COUNT] = '\0';
    display.print(text);
}

void enterPhase(Phase next) {
    phase = next;
    phaseStartMs = millis();
    glyphIndex = 0;
    stringIndex = 0;

    switch (phase) {
        case Phase::AllSegments:
            display.print("8888");
            Serial.println("phase: all segments");
            break;

        case Phase::Glyphs:
            showGlyph(GLYPHS[0]);
            Serial.println("phase: glyphs");
            break;

        case Phase::Dots:
            display.print("1234");
            for (uint8_t i = 0; i < MFShieldDisplay::DIGIT_COUNT; ++i) {
                display.setDecimalPoint(i, true);
            }
            Serial.println("phase: decimal points");
            break;

        case Phase::Strings:
            display.print(STRINGS[0]);
            Serial.println("phase: strings");
            break;

        case Phase::Brightness:
            brightness = 3;
            display.setBrightness(brightness);
            display.print("8888");
            Serial.println("phase: brightness");
            break;
    }
}

void handleButtons() {
    if (pressed(buttons[0])) {
        wiringInverted = !wiringInverted;
        display.setWiring(wiringInverted
                              ? MFShieldDisplay::Wiring::InvertedClone
                              : MFShieldDisplay::Wiring::Classic);
        Serial.println(wiringInverted ? "wiring: InvertedClone"
                                      : "wiring: Classic");
    }

    if (pressed(buttons[1])) {
        digitsReversed = !digitsReversed;
        display.setReverseDigits(digitsReversed);
        Serial.println(digitsReversed ? "digit order: reversed"
                                      : "digit order: normal");
    }

    if (pressed(buttons[2])) {
        brightness = static_cast<uint8_t>((brightness + 1) % 4);
        display.setBrightness(brightness);
        Serial.print("brightness: ");
        Serial.println(brightness);
    }
}

void handlePhases() {
    const uint32_t now = millis();

    switch (phase) {
        case Phase::AllSegments:
            if ((now - phaseStartMs) >= ALL_SEG_MS) {
                enterPhase(Phase::Glyphs);
            }
            break;

        case Phase::Glyphs:
            if ((now - phaseStartMs) >= GLYPH_MS) {
                ++glyphIndex;
                if (GLYPHS[glyphIndex] == '\0') {
                    enterPhase(Phase::Dots);
                } else {
                    phaseStartMs = now;
                    showGlyph(GLYPHS[glyphIndex]);
                    Serial.print("glyph: ");
                    Serial.println(GLYPHS[glyphIndex]);
                }
            }
            break;

        case Phase::Dots:
            if ((now - phaseStartMs) >= DOTS_MS) {
                enterPhase(Phase::Strings);
            }
            break;

        case Phase::Strings:
            if ((now - phaseStartMs) >= STRING_MS) {
                ++stringIndex;
                if (stringIndex >= STRING_COUNT) {
                    enterPhase(Phase::Brightness);
                } else {
                    phaseStartMs = now;
                    display.print(STRINGS[stringIndex]);
                    Serial.print("string: ");
                    Serial.println(STRINGS[stringIndex]);
                }
            }
            break;

        case Phase::Brightness:
            if ((now - phaseStartMs) >= BRIGHTNESS_MS) {
                if (brightness == 0) {
                    display.setBrightness(3);
                    enterPhase(Phase::AllSegments);
                } else {
                    --brightness;
                    display.setBrightness(brightness);
                    phaseStartMs = now;
                    Serial.print("brightness: ");
                    Serial.println(brightness);
                }
            }
            break;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);

    pinMode(PIN_BTN_S1, INPUT_PULLUP);
    pinMode(PIN_BTN_S2, INPUT_PULLUP);
    pinMode(PIN_BTN_S3, INPUT_PULLUP);

    display.begin();
    display.setBrightness(3);

    Serial.println();
    Serial.println("MFShieldDisplay test");
    Serial.println("S1: wiring, S2: digit order, S3: brightness");

    enterPhase(Phase::AllSegments);
}

void loop() {
    // Динамическая индикация: вызывать как можно чаще.
    display.refresh();

    handleButtons();
    handlePhases();
}
