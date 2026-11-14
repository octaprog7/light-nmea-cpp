// ============================================================================
// main.cpp - диагностика обмена с GNSS-модулем (Arduino UNO R3).
//
// Задача: проверить не только приём NMEA, но и передачу в модуль
// (чтение конфигурации по UBX). Фикс для проверки не требуется.
//
// Подключение:
//   GNSS TX  (синий)   -> D5  (SoftwareSerial RX)
//   GNSS RX  (зелёный) -> D6  (SoftwareSerial TX)
//   GNSS VCC (красный) -> 5V
//   GNSS GND (чёрный)  -> GND
//
// Что печатает:
//   NMEA: <строка>                    - всё, что пришло от модуля;
//   UBX: cls=.. id=.. len=..          - заголовок любого UBX-пакета;
//     MON-VER sw=... hw=...           - версия прошивки и железа;
//     CFG-PRT baud=... inMask=... outMask=... - конфигурация порта;
//   --> UBX-... poll                  - что отправили модулю.
// ============================================================================

#include <Arduino.h>
#include <SoftwareSerial.h>

namespace {

constexpr uint8_t PIN_GNSS_RX = 5;  // D5 <- TX модуля (синий)
constexpr uint8_t PIN_GNSS_TX = 6;  // D6 -> RX модуля (зелёный)

constexpr uint32_t GNSS_BAUD = 38400;
constexpr uint32_t USB_BAUD = 115200;
constexpr uint32_t POLL_INTERVAL_MS = 3000;

constexpr size_t LINE_MAX = 96;
// MON-VER у M10 содержит много строк-расширений и легко превышает 128 байт.
constexpr size_t UBX_MAX = 200;

// UBX-MON-VER, пустой payload (запрос).
const uint8_t POLL_MON_VER[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
// UBX-CFG-PRT, пустой payload (запрос конфигурации порта).
const uint8_t POLL_CFG_PRT[] = {0xB5, 0x62, 0x06, 0x00, 0x00, 0x00, 0x06, 0x18};

SoftwareSerial gnss(PIN_GNSS_RX, PIN_GNSS_TX);

char line[LINE_MAX];
size_t lineLen = 0;

uint8_t ubx[UBX_MAX];
size_t ubxLen = 0;
bool ubxSync = false;  // получен 0xB5, ждём 0x62

uint32_t lastPollMs = 0;
uint8_t pollStep = 0;
uint32_t nmeaCount = 0;
uint32_t ubxCount = 0;

void printHex(uint8_t value) {
    Serial.print("0123456789ABCDEF"[value >> 4]);
    Serial.print("0123456789ABCDEF"[value & 0x0F]);
}

void printHex16(uint16_t value) {
    printHex(static_cast<uint8_t>(value >> 8));
    printHex(static_cast<uint8_t>(value));
}

void printNmeaLine() {
    if (lineLen == 0) {
        return;
    }
    line[lineLen] = '\0';
    ++nmeaCount;
    Serial.print("NMEA: ");
    Serial.println(line);
    lineLen = 0;
}

/// Длина UBX-пакета по заголовку (6 байт + payload + 2 байта контрольной суммы).
size_t ubxTotalLength() {
    if (ubxLen < 6) {
        return 0;
    }
    const uint16_t payloadLen =
        static_cast<uint16_t>(ubx[4]) |
        (static_cast<uint16_t>(ubx[5]) << 8);
    return 6u + payloadLen + 2u;
}

void handleUbx() {
    const size_t total = ubxTotalLength();
    if (total == 0 || ubxLen < total) {
        return;
    }

    const uint8_t cls = ubx[2];
    const uint8_t id = ubx[3];
    const uint16_t payloadLen = static_cast<uint16_t>(ubx[4]) |
                                (static_cast<uint16_t>(ubx[5]) << 8);

    uint8_t ckA = 0;
    uint8_t ckB = 0;
    for (size_t i = 2; i < total - 2; ++i) {
        ckA = static_cast<uint8_t>(ckA + ubx[i]);
        ckB = static_cast<uint8_t>(ckB + ckA);
    }

    if (ckA != ubx[total - 2] || ckB != ubx[total - 1]) {
        Serial.print("UBX: bad checksum, cls=0x");
        printHex(cls);
        Serial.print(" id=0x");
        printHex(id);
        Serial.println();
        ubxLen = 0;
        return;
    }

    ++ubxCount;
    Serial.print("UBX: cls=0x");
    printHex(cls);
    Serial.print(" id=0x");
    printHex(id);
    Serial.print(" len=");
    Serial.println(payloadLen);

    if (cls == 0x0A && id == 0x04 && payloadLen >= 40) {
        // swVersion[30] + hwVersion[10] + расширения.
        Serial.print("  MON-VER sw=");
        Serial.println(reinterpret_cast<const char*>(&ubx[6]));
        Serial.print("  MON-VER hw=");
        Serial.println(reinterpret_cast<const char*>(&ubx[6 + 30]));
    } else if (cls == 0x06 && id == 0x00 && payloadLen >= 20) {
        // portID(1) reserved(1) txReady(2) mode(4) baudRate(4) in/outMask(2+2)...
        const uint8_t* p = &ubx[6];
        const uint32_t baud = static_cast<uint32_t>(p[8]) |
                              (static_cast<uint32_t>(p[9]) << 8) |
                              (static_cast<uint32_t>(p[10]) << 16) |
                              (static_cast<uint32_t>(p[11]) << 24);
        const uint16_t inMask =
            static_cast<uint16_t>(p[12]) | (static_cast<uint16_t>(p[13]) << 8);
        const uint16_t outMask =
            static_cast<uint16_t>(p[14]) | (static_cast<uint16_t>(p[15]) << 8);

        Serial.print("  CFG-PRT baud=");
        Serial.print(baud);
        Serial.print(" inMask=0x");
        printHex16(inMask);
        Serial.print(" outMask=0x");
        printHex16(outMask);
        Serial.println();
    }

    ubxLen = 0;
}

void feed(uint8_t b) {
    if (ubxSync) {
        ubxSync = false;
        if (b == 0x62) {
            ubx[0] = 0xB5;
            ubx[1] = 0x62;
            ubxLen = 2;
            return;
        }
    }

    if (ubxLen >= 2) {
        if (ubxLen < UBX_MAX) {
            ubx[ubxLen++] = b;
        } else {
            ubxLen = 0;
            return;
        }
        const size_t total = ubxTotalLength();
        if (total > UBX_MAX) {
            Serial.print("UBX: frame too long, need ");
            Serial.print(total);
            Serial.print(" bytes, buffer ");
            Serial.println(UBX_MAX);
            ubxLen = 0;
            return;
        }
        if (total != 0 && ubxLen >= total) {
            handleUbx();
        }
        return;
    }

    if (b == 0xB5) {
        ubxSync = true;
        return;
    }

    if (b == '\n') {
        printNmeaLine();
    } else if (b == '\r') {
        // игнорируем
    } else if (lineLen < LINE_MAX - 1) {
        line[lineLen++] = static_cast<char>(b);
    } else {
        lineLen = 0;  // строка не влезла - сбрасываем
    }
}

}  // namespace

void setup() {
    Serial.begin(USB_BAUD);
    while (!Serial && millis() < 2000) {
        ;
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("  GNSS probe (NMEA + UBX)");
    Serial.println("========================================");
    Serial.print("GNSS RX (D");
    Serial.print(PIN_GNSS_RX);
    Serial.print(") <- TX модуля (синий), GNSS TX (D");
    Serial.print(PIN_GNSS_TX);
    Serial.println(") -> RX модуля (зелёный)");
    Serial.print("GNSS baud: ");
    Serial.println(GNSS_BAUD);
    Serial.println("========================================");

    gnss.begin(GNSS_BAUD);
}

void loop() {
    while (gnss.available()) {
        feed(static_cast<uint8_t>(gnss.read()));
    }

    if (millis() - lastPollMs >= POLL_INTERVAL_MS) {
        lastPollMs = millis();

        if (pollStep == 0) {
            Serial.print("--> UBX-MON-VER poll (NMEA=");
            Serial.print(nmeaCount);
            Serial.print(", UBX=");
            Serial.print(ubxCount);
            Serial.println(")");
            gnss.write(POLL_MON_VER, sizeof(POLL_MON_VER));
        } else {
            Serial.println("--> UBX-CFG-PRT poll");
            gnss.write(POLL_CFG_PRT, sizeof(POLL_CFG_PRT));
        }

        pollStep = static_cast<uint8_t>((pollStep + 1) % 2);
    }
}
