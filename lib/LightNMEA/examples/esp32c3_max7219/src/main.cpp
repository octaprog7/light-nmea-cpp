// ============================================================================
// main.cpp - GNSS (NMEA) на 7-сегментный индикатор MAX7219 (8 знакомест).
//
// Плата: ESP32-C3. Дисплей: MAX7219 по аппаратному SPI.
//   SCK = GPIO4, MOSI = GPIO6, MISO = GPIO5 (не используется), CS = GPIO7.
//
// GNSS-модуль (38400, 8N1) на UART1:
//   TX модуля (синий)   -> GPIO3 (UART1 RX, вход платы)
//   RX модуля (зелёный) -> GPIO2 (UART1 TX, выход платы; можно не подключать)
//   GPIO2 - strapping-пин: при старте должен быть высоким. RX модуля - вход,
//   поэтому он его не тянет; нельзя ставить на GPIO2 подтяжку к земле.
//   VCC (красный)       -> 3V3 или 5V
//   GND (чёрный)        -> GND
// Провода идут ПЕРЕКРЁСТНО: TX модуля -> RX платы, RX модуля -> TX платы.
//
// UART0 (GPIO20/21) НЕ использовать: на этой плате он занят USB-мостом CH343,
// через него идёт лог в /dev/ttyACM0. GNSS на UART0 глушит USB-обмен.
//
// Страницы чередуются каждые PAGE_MS (3 секунды):
//   1) время  - HH-MM-SS (UTC, без смещения часового пояса);
//   2) широта - десятичные градусы, 8 знакомест (например 12.34567);
//   3) долгота - десятичные градусы, 8 знакомест (например 45.67890).
// Пока нет валидного фикса - на дисплее "--------".
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <cmath>
#include <cstdio>
#include <string_view>

#include <light_nmea.hpp>

#include "max7219.hpp"
#include "seg_display.hpp"

namespace {

// ----------------------------------------------------------------------------
// Дисплей MAX7219
// ----------------------------------------------------------------------------
constexpr int PIN_SCK  = 4;
constexpr int PIN_MISO = 5;
constexpr int PIN_MOSI = 6;
constexpr int PIN_CS   = 7;
constexpr int PIN_SS   = -1;  // CS управляю вручную

constexpr uint8_t DISPLAY_COLUMNS = 8;
constexpr uint8_t BRIGHTNESS      = 9;

MAX7219 displayController(PIN_CS);
SegDisplay display(displayController);

// ----------------------------------------------------------------------------
// GNSS на UART1 (UART0 занят USB-мостом CH343)
// ----------------------------------------------------------------------------
constexpr int PIN_GNSS_RX = 3;  // UART1 RX: сюда идёт TX модуля (синий)
constexpr int PIN_GNSS_TX = 2;  // UART1 TX: сюда идёт RX модуля (зелёный)
constexpr uint32_t GNSS_BAUD = 38400;
constexpr uint32_t USB_BAUD  = 115200;

constexpr uint32_t PAGE_MS = 3000;
// Не LINE_MAX: на ESP32 это имя занято макросом из sys/param.h.
constexpr size_t NMEA_LINE_MAX = 96;

// UART1: Serial (UART0) остаётся USB-портом для логов через CH343.
HardwareSerial gnssSerial(1);

light_nmea::Parser parser;

char line[NMEA_LINE_MAX];
size_t lineLen = 0;

uint8_t page = 0;
uint32_t lastPageMs = 0;
uint32_t lastRenderedTime = 0xFFFFFFFFu;
bool forceRedraw = true;

/// Время HHMMSS (UTC) -> "HH-MM-SS" (8 знакомест).
void formatClock(uint32_t timeUtc, char* buf, size_t size) noexcept {
    const int hours = static_cast<int>(timeUtc / 10000u);
    const int minutes = static_cast<int>((timeUtc / 100u) % 100u);
    const int seconds = static_cast<int>(timeUtc % 100u);
    std::snprintf(buf, size, "%02d-%02d-%02d", hours, minutes, seconds);
}

/// Координата в десятичных градусах, максимум 8 знакомест.
/// Для |значение| < 100 - 5 знаков после точки, иначе 4 (чтобы влезть).
void formatCoord(double value, char* buf, size_t size) noexcept {
    const int decimals = (std::fabs(value) >= 100.0) ? 4 : 5;
    std::snprintf(buf, size, "%.*f", decimals, value);
}

/// Рисует текущую страницу по последним данным парсера.
void render() {
    const auto& d = parser.data();
    char buf[16];

    display.clear();

    if (!d.valid || !d.has_coordinates()) {
        display.show("--------");
        return;
    }

    switch (page) {
        case 0:
            formatClock(d.time_utc, buf, sizeof(buf));
            break;
        case 1:
            formatCoord(static_cast<double>(d.latitude_deg()), buf, sizeof(buf));
            break;
        default:
            formatCoord(static_cast<double>(d.longitude_deg()), buf, sizeof(buf));
            break;
    }

    display.show(buf);
}

}  // namespace

void setup() {
    Serial.begin(USB_BAUD);
    delay(300);  // время на перечисление USB
    Serial.println();
    Serial.println("BOOT: GNSS -> MAX7219 (8 digits)");

    // Дисплей
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
    displayController.begin(SPI, DISPLAY_COLUMNS);
    displayController.setBrightness(BRIGHTNESS);
    display.init();

    display.show("88888888");  // проверка всех сегментов
    delay(500);
    display.clear();
    display.show("--------");

    // GNSS
    gnssSerial.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
    Serial.print("GNSS UART1: RX=GPIO");
    Serial.print(PIN_GNSS_RX);
    Serial.print(", TX=GPIO");
    Serial.print(PIN_GNSS_TX);
    Serial.print(", ");
    Serial.print(GNSS_BAUD);
    Serial.println(" 8N1");

    lastPageMs = millis();
}

void loop() {
    // Читаю NMEA из UART0 и разбираю построчно.
    while (gnssSerial.available()) {
        const char c = static_cast<char>(gnssSerial.read());
        if (c == '\n' || c == '\r') {
            if (lineLen > 0) {
                line[lineLen] = '\0';
                (void)parser.parse_line(std::string_view(line));
                lineLen = 0;
            }
        } else if (lineLen < NMEA_LINE_MAX - 1) {
            line[lineLen++] = c;
        } else {
            lineLen = 0;  // строка не влезла - сбрасываю
        }
    }

    // Переключение страниц: время -> широта -> долгота.
    const uint32_t now = millis();
    if (now - lastPageMs >= PAGE_MS) {
        lastPageMs = now;
        page = static_cast<uint8_t>((page + 1) % 3);
        forceRedraw = true;
    }

    // Перерисовываю при смене страницы и при обновлении времени (раз в секунду).
    const auto& d = parser.data();
    if (forceRedraw || d.time_utc != lastRenderedTime) {
        lastRenderedTime = d.time_utc;
        forceRedraw = false;
        render();

        Serial.print("page=");
        Serial.print(page);
        Serial.print(" valid=");
        Serial.print(d.valid ? 1 : 0);
        Serial.print(" sats=");
        Serial.print(static_cast<unsigned>(d.satellites));
        Serial.print(" utc=");
        Serial.print(static_cast<unsigned long>(d.time_utc));
        Serial.print(" lat=");
        Serial.print(static_cast<double>(d.latitude_deg()), 6);
        Serial.print(" lon=");
        Serial.println(static_cast<double>(d.longitude_deg()), 6);
    }
}
