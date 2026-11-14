/*
 * LightNMEA GPS Logger - Cross-Platform Firmware
 * Copyright (C) 2026 Roman Shevchik
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <Arduino.h>
#include <light_nmea.hpp>
#include <nmea_formatter.hpp>  // detail::CST_NAMES / FIX_NAMES / get_name

#if defined(LIGHTNMEA_USE_MFSHIELD)
    #include <mfshield_display.hpp>
#endif

// ============================================================================
// Platform Detection and Configuration
// ============================================================================

#if defined(ARDUINO_ARCH_RP2040)
    // Raspberry Pi Pico (RP2040)
    #define PLATFORM_NAME "RP2040 (Pico)"
    #define GPS_SERIAL Serial1
    #define GPS_RX_PIN 1      
    #define GPS_TX_PIN 0      
    #define LED_PIN LED_BUILTIN
    #define HAS_HARDWARE_SERIAL true
    
#elif defined(ARDUINO_ARCH_STM32)
    // STM32 (Blue Pill, Nucleo, etc.)
    #define PLATFORM_NAME "STM32"
    #define GPS_SERIAL Serial1
    #define GPS_RX_PIN PA3  // Default for STM32
    #define GPS_TX_PIN PA2  // Default for STM32
    #define LED_PIN PC13
    #define HAS_HARDWARE_SERIAL true
    
#elif defined(ESP32)
    // ESP32
    #define PLATFORM_NAME "ESP32"
    #define GPS_SERIAL Serial1
    #define GPS_RX_PIN 16
    #define GPS_TX_PIN 17
    #define LED_PIN LED_BUILTIN
    #define HAS_HARDWARE_SERIAL true
    
#elif defined(ESP8266)
    // ESP8266 (NodeMCU, Wemos D1, etc.)
    #define PLATFORM_NAME "ESP8266"
    #define GPS_SERIAL Serial
    #define GPS_RX_PIN 3      // RX0
    #define GPS_TX_PIN 1      // TX0 (not used)
    #define LED_PIN LED_BUILTIN
    #define HAS_HARDWARE_SERIAL true
    #warning "ESP8266 uses Serial for GPS. USB debugging disabled."
    
#elif defined(__AVR__)
    // AVR (Arduino Uno, Nano, Mega)
    #if defined(__AVR_ATmega2560__)
        // Arduino Mega has multiple hardware serials
        #define PLATFORM_NAME "AVR (Mega)"
        #define GPS_SERIAL Serial1
        #define GPS_RX_PIN 19
        #define GPS_TX_PIN 18
    #else
        // Uno/Nano: use SoftwareSerial.
        // D4 занят защёлкой (LATCH) индикатора Multi-Function Shield,
        // поэтому GPS переведён на свободные D5 (RX) / D6 (TX).
        #define PLATFORM_NAME "AVR (Uno/Nano)"
        #define USE_SOFTWARE_SERIAL
        #define GPS_RX_PIN 5
        #define GPS_TX_PIN 6
    #endif
    #define LED_PIN LED_BUILTIN
    #define HAS_HARDWARE_SERIAL (defined(__AVR_ATmega2560__))
    
#elif defined(ARDUINO_ARCH_SAMD)
    // SAMD (Arduino Zero, Nano 33)
    #define PLATFORM_NAME "SAMD"
    #define GPS_SERIAL Serial1
    #define GPS_RX_PIN 13
    #define GPS_TX_PIN 14
    #define LED_PIN LED_BUILTIN
    #define HAS_HARDWARE_SERIAL true
    
#else
    // Unknown platform
    #error "Unsupported platform! Please add configuration for your board."
#endif

// ============================================================================
// Global Constants
// ============================================================================
constexpr uint32_t GPS_BAUD_RATE = 38400;  
constexpr uint32_t USB_BAUD_RATE = 115200;
// NMEA 0183 ограничивает предложение 82 символами (включая CR/LF);
// 128 байт - с запасом. На Arduino UNO (2 КБ SRAM) это экономит 384 байта.
constexpr size_t BUFFER_SIZE = 128;

// ============================================================================
// Global Variables
// ============================================================================
light_nmea::Parser parser;
char serialBuffer[BUFFER_SIZE];
uint16_t bufferIndex = 0;
static uint32_t last_printed_time = 0;
static uint32_t last_led_toggle = 0;
static bool led_state = false;

// Диагностика без фикса (включается -DLIGHTNMEA_STATUS_NOFIX=1):
// раз в секунду печатаем состояние парсера, чтобы видеть, что NMEA
// разбирается, даже когда координат ещё нет.
#if defined(LIGHTNMEA_STATUS_NOFIX)
    static uint32_t last_status_ms = 0;
    static uint32_t parsed_lines = 0;
    static uint32_t cnt_rmc = 0;
    static uint32_t cnt_gga = 0;
    static uint32_t cnt_vtg = 0;
    static uint32_t cnt_gll = 0;
#endif

#if defined(LIGHTNMEA_USE_MFSHIELD)
    // Индикатор Multi-Function Shield (2 x 74HC595): D4=LATCH, D7=CLOCK, D8=DATA.
    MFShieldDisplay shieldDisplay;

    // Пока не пришло время от GNSS - прокручиваем поддерживаемые символы.
    static const char* const DISPLAY_DEMO[] = {
        "0123", "4567", "89AC", "EFHL", "PU-."
    };
    static bool display_has_time = false;
    static uint32_t last_demo_ms = 0;
    static uint8_t demo_index = 0;
#endif

#ifdef USE_SOFTWARE_SERIAL
    #include <SoftwareSerial.h>
    SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
#endif

// ============================================================================
// Helper Functions
// ============================================================================

void toggle_led() {
    led_state = !led_state;
    digitalWrite(LED_PIN, led_state ? HIGH : LOW);
}

/// Печать целого двумя разрядами с ведущим нулём (без printf).
/// Шаблон, чтобы работать и с Arduino Print, и с родным SerialOut.
template <typename Out>
static void print2(Out& out, int value) {
    out.print(static_cast<char>('0' + (value / 10) % 10));
    out.print(static_cast<char>('0' + value % 10));
}

/// Печать числа с фиксированной точностью без printf.
/// Нужно для AVR: Serial.printf отсутствует, а snprintf("%f") требует
/// отдельной связки printf-floating point.
template <typename Out>
static void printFixed(Out& out, double value, uint8_t decimals) {
    bool negative = value < 0.0;
    if (negative) value = -value;

    uint32_t scale = 1;
    for (uint8_t i = 0; i < decimals; ++i) scale *= 10;

    const uint32_t scaled =
        static_cast<uint32_t>(value * static_cast<double>(scale) + 0.5);

    if (negative && scaled != 0) out.print('-');
    out.print(static_cast<uint32_t>(scaled / scale));

    if (decimals > 0) {
        out.print('.');
        const uint32_t frac = scaled % scale;
        for (uint32_t div = scale / 10; div > 0; div /= 10) {
            out.print(static_cast<char>('0' + (frac / div) % 10));
        }
    }
}

void output_csv(const light_nmea::NMEAData& data) {
    // Parse compact UTC time (HHMMSS) and UTC date (DDMMYY)
    uint32_t t = data.time_utc;
    uint32_t d = data.date;
    
    int h = t / 10000;
    int m = (t / 100) % 100;
    int s = t % 100;
    
    int day = d / 10000;

#if defined(LIGHTNMEA_USE_MFSHIELD)
    // На индикатор шилда - время UTC в формате HH.MM (точка между часами
    // и минутами, 4 знакоместа). Значение 0 означает "времени ещё нет".
    if (t != 0) {
        char clockText[6];
        clockText[0] = static_cast<char>('0' + (h / 10) % 10);
        clockText[1] = static_cast<char>('0' + h % 10);
        clockText[2] = '.';
        clockText[3] = static_cast<char>('0' + (m / 10) % 10);
        clockText[4] = static_cast<char>('0' + m % 10);
        clockText[5] = '\0';
        shieldDisplay.print(clockText);
        display_has_time = true;
    }
#endif
    int mon = (d / 100) % 100;
    int yr = d % 100;

    // Canonical 12-field CSV stream. The field order and formats follow the
    // LOG_CSV_HEADER contract of the PC-side tools (dash_utils.hpp /
    // dash_utils.py): nmea_pc_logger.py and gnss_dashboard expect exactly
    // valid,satellites,latitude,longitude,speed,course,altitude,time,date,
    // constellation,fix_mode,hdop. Time is "HH:MM:SS", date is "DD.MM.20YY"
    // (both UTC); constellation and fix-mode are NAMES (not numeric enums),
    // matching GNSSData::from_csv()/from_parser().
    //
    // Печать через Print, а не printf: на AVR (Arduino UNO) нет
    // HardwareSerial::printf, а snprintf("%f") требует printf-floating point.
    Serial.print(data.valid ? 1 : 0);               // 1. Valid (0/1)
    Serial.print(',');
    Serial.print(static_cast<unsigned>(data.satellites)); // 2. Satellites
    Serial.print(',');
    printFixed(Serial, data.latitude_deg(), 6);     // 3. Latitude (deg)
    Serial.print(',');
    printFixed(Serial, data.longitude_deg(), 6);    // 4. Longitude (deg)
    Serial.print(',');
    printFixed(Serial, data.speed_kmh(), 1);        // 5. Speed (km/h)
    Serial.print(',');
    printFixed(Serial, data.course_true_deg(), 1);  // 6. Course true (deg)
    Serial.print(',');
    printFixed(Serial, data.altitude_m(), 1);       // 7. Altitude (m)
    Serial.print(',');
    print2(Serial, h);                              // 8. Time (UTC) HH:MM:SS
    Serial.print(':');
    print2(Serial, m);
    Serial.print(':');
    print2(Serial, s);
    Serial.print(',');
    print2(Serial, day);                            // 9. Date (UTC) DD.MM.20YY
    Serial.print('.');
    print2(Serial, mon);
    Serial.print(".20");
    print2(Serial, yr);
    Serial.print(',');
    Serial.print(light_nmea::detail::get_name(      // 10. Constellation
        light_nmea::detail::CST_NAMES,
        static_cast<uint8_t>(data.constellation)));
    Serial.print(',');
    Serial.print(light_nmea::detail::get_name(      // 11. Fix mode
        light_nmea::detail::FIX_NAMES,
        static_cast<uint8_t>(data.fix_mode)));
    Serial.print(',');
    printFixed(Serial, data.hdop_value(), 2);       // 12. HDOP
    Serial.println();
}

void process_gps_data() {
    if (bufferIndex > 0) {
        serialBuffer[bufferIndex] = '\0';
        
        if (parser.parse_line(std::string_view(serialBuffer))) {
            const auto& data = parser.data();
#if defined(LIGHTNMEA_STATUS_NOFIX)
            ++parsed_lines;
            switch (data.msg_type) {
                case light_nmea::MessageType::RMC: ++cnt_rmc; break;
                case light_nmea::MessageType::GGA: ++cnt_gga; break;
                case light_nmea::MessageType::VTG: ++cnt_vtg; break;
                case light_nmea::MessageType::GLL: ++cnt_gll; break;
                default: break;
            }
#endif
            
            if (data.has_coordinates() && data.valid) {
                if (data.time_utc != last_printed_time) {
                    last_printed_time = data.time_utc;
                    output_csv(data);
                    toggle_led(); // Blink LED on valid GPS data
                }
            }
        }
        bufferIndex = 0;
    }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // Initialize USB Serial (for debugging/output)
#ifndef ESP8266
    Serial.begin(USB_BAUD_RATE);
    while (!Serial && millis() < 2000) {
        ; // Wait for USB serial (needed for some boards)
    }
#else
    // ESP8266: Serial is used for GPS
#endif

    // Print platform info
    Serial.println();
    Serial.println("========================================");
    Serial.println("  LightNMEA GPS Logger");
    Serial.println("========================================");
    Serial.print("Platform: ");
    Serial.println(PLATFORM_NAME);
    Serial.print("GPS RX Pin: ");
    Serial.println(GPS_RX_PIN);
    Serial.print("GPS TX Pin: ");
    Serial.println(GPS_TX_PIN);
    Serial.print("Baud Rate: ");
    Serial.println(GPS_BAUD_RATE);
    Serial.println("========================================");
    Serial.println("Waiting for GPS data...");
    Serial.println();

#if defined(LIGHTNMEA_USE_MFSHIELD)
    // Индикатор Multi-Function Shield: тест всех сегментов ~0.5 с,
    // затем прочерки до прихода времени от GNSS.
    shieldDisplay.begin();
    shieldDisplay.print("8888");
    {
        const uint32_t testStart = millis();
        while (millis() - testStart < 500) {
            shieldDisplay.refresh();
        }
    }
    shieldDisplay.print("----");
    Serial.println("Display: Multi-Function Shield (2 x 74HC595) initialized");
#endif

    // Initialize GPS Serial
#ifdef USE_SOFTWARE_SERIAL
    gpsSerial.begin(GPS_BAUD_RATE);
    Serial.println("Using SoftwareSerial for GPS");
#elif defined(ARDUINO_ARCH_RP2040)
    // Для Raspberry Pi Pico пины настраиваются отдельно
    GPS_SERIAL.setRX(GPS_RX_PIN);
    GPS_SERIAL.setTX(GPS_TX_PIN);
    GPS_SERIAL.begin(GPS_BAUD_RATE);
    Serial.println("GPS UART initialized (RP2040)");
#else
    // Для STM32, ESP32 и других
    GPS_SERIAL.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("GPS UART initialized");
#endif

    // Blink LED 3 times to indicate startup
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
    }
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
#if defined(LIGHTNMEA_USE_MFSHIELD)
    // Динамическая индикация: переключаем разряды как можно чаще.
    shieldDisplay.refresh();
#endif

    // Read from GPS UART
#ifdef USE_SOFTWARE_SERIAL
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
#else
    while (GPS_SERIAL.available()) {
        char c = GPS_SERIAL.read();
#endif
        if (c == '\n' || c == '\r') {
            process_gps_data();
        } else {
            if (bufferIndex < BUFFER_SIZE - 1) {
                serialBuffer[bufferIndex++] = c;
            } else {
                // Buffer overflow - reset
                bufferIndex = 0;
            }
        }
    }
    
#if defined(LIGHTNMEA_STATUS_NOFIX)
    if (millis() - last_status_ms >= 1000) {
        last_status_ms = millis();
        const auto& d = parser.data();
        Serial.print("# status: lines=");
        Serial.print(parsed_lines);
        Serial.print(" rmc=");
        Serial.print(cnt_rmc);
        Serial.print(" gga=");
        Serial.print(cnt_gga);
        Serial.print(" vtg=");
        Serial.print(cnt_vtg);
        Serial.print(" gll=");
        Serial.print(cnt_gll);
        Serial.print(" sats=");
        Serial.print(static_cast<unsigned>(d.satellites));
        Serial.print(" hdop=");
        Serial.print(d.hdop_value(), 2);
        Serial.print(" alt=");
        Serial.print(d.altitude_m(), 1);
        Serial.print(" valid=");
        Serial.print(d.valid ? 1 : 0);
        Serial.print(" fix=");
        Serial.print(static_cast<unsigned>(d.fix_mode));
        Serial.print(" utc=");
        Serial.println(static_cast<unsigned long>(d.time_utc));
    }
#endif

    // Toggle LED every 500ms to indicate system is alive
    if (millis() - last_led_toggle > 500) {
        last_led_toggle = millis();
        // Only toggle if we haven't just blinked for GPS data
        if (!led_state) {
            digitalWrite(LED_PIN, HIGH);
        } else {
            digitalWrite(LED_PIN, LOW);
        }
    }

#if defined(LIGHTNMEA_USE_MFSHIELD)
    // Пока нет времени от GNSS - прокручиваем все поддерживаемые символы,
    // чтобы индикатор можно было проверить без подключённого модуля.
    if (!display_has_time && (millis() - last_demo_ms) >= 800) {
        last_demo_ms = millis();
        shieldDisplay.print(DISPLAY_DEMO[demo_index]);
        demo_index = static_cast<uint8_t>(
            (demo_index + 1) % (sizeof(DISPLAY_DEMO) / sizeof(DISPLAY_DEMO[0])));
    }
#endif
}