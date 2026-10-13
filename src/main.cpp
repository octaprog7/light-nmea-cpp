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
        // Uno/Nano: use SoftwareSerial
        #define PLATFORM_NAME "AVR (Uno/Nano)"
        #define USE_SOFTWARE_SERIAL
        #define GPS_RX_PIN 4
        #define GPS_TX_PIN 5
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
constexpr size_t BUFFER_SIZE = 512;        

// ============================================================================
// Global Variables
// ============================================================================
light_nmea::Parser parser;
char serialBuffer[BUFFER_SIZE];
uint16_t bufferIndex = 0;
static uint32_t last_printed_time = 0;
static uint32_t last_led_toggle = 0;
static bool led_state = false;

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

void output_csv(const light_nmea::NMEAData& data) {
    // Parse compact UTC time (HHMMSS) and UTC date (DDMMYY)
    uint32_t t = data.time_utc;
    uint32_t d = data.date;
    
    int h = t / 10000;
    int m = (t / 100) % 100;
    int s = t % 100;
    
    int day = d / 10000;
    int mon = (d / 100) % 100;
    int yr = d % 100;

    // Canonical 12-field CSV stream. The field order and formats follow the
    // LOG_CSV_HEADER contract of the PC-side tools (dash_utils.hpp /
    // dash_utils.py): nmea_pc_logger.py and gnss_dashboard expect exactly
    // valid,satellites,latitude,longitude,speed,course,altitude,time,date,
    // constellation,fix_mode,hdop. Time is "HH:MM:SS", date is "DD.MM.20YY"
    // (both UTC); constellation and fix-mode are NAMES (not numeric enums),
    // matching GNSSData::from_csv()/from_parser().
    Serial.printf("%d,%u,%.6f,%.6f,%.1f,%.1f,%.1f,%02d:%02d:%02d,%02d.%02d.20%02d,%s,%s,%.2f\n",
        data.valid ? 1 : 0,                 // 1. Valid (0/1)
        static_cast<unsigned>(data.satellites), // 2. Satellites
        data.latitude_deg(),                // 3. Latitude (deg)
        data.longitude_deg(),               // 4. Longitude (deg)
        data.speed_kmh(),                   // 5. Speed (km/h)
        data.course_true_deg(),             // 6. Course true (deg)
        data.altitude_m(),                  // 7. Altitude (m)
        h, m, s,                            // 8. Time (UTC) HH:MM:SS
        day, mon, yr,                       // 9. Date (UTC) DD.MM.20YY
        light_nmea::detail::get_name(light_nmea::detail::CST_NAMES,
                                     static_cast<uint8_t>(data.constellation)), // 10. Constellation
        light_nmea::detail::get_name(light_nmea::detail::FIX_NAMES,
                                     static_cast<uint8_t>(data.fix_mode)),      // 11. Fix mode
        data.hdop_value()                   // 12. HDOP
    );
}

void process_gps_data() {
    if (bufferIndex > 0) {
        serialBuffer[bufferIndex] = '\0';
        
        if (parser.parse_line(std::string_view(serialBuffer))) {
            const auto& data = parser.data();
            
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
}