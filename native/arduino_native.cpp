/*
 * arduino_native.cpp - host implementation of the Arduino shim.
 *
 * Pairs with native/Arduino.h. Provides:
 *   - SerialOut ("Serial")  writing to stdout
 *   - SerialIn  ("Serial1") reading from stdin (NMEA source on the host)
 *   - millis() / delay() / pinMode() / digitalWrite()
 *   - main() that runs setup() + an endless loop(), like the Arduino core.
 *
 * Pipe model:  cat track.nmea | ./build-native/lightnmea_logger
 * The process exits cleanly when stdin reaches EOF.
 */

#include "Arduino.h"

#include <thread>

// ============================================================================
// Time helpers
// ============================================================================
unsigned long millis() {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<unsigned long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

unsigned long micros() {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<unsigned long>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - start).count());
}

void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ============================================================================
// GPIO (no-ops on the host; pin state is irrelevant without hardware)
// ============================================================================
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}

// ============================================================================
// Serial (debug output -> stdout)
// ============================================================================
void SerialOut::begin(unsigned long) {}

void SerialOut::print(const char *s) { fputs(s, stdout); }
void SerialOut::print(char c) { fputc(c, stdout); }
void SerialOut::print(int v) { fprintf(stdout, "%d", v); }
void SerialOut::print(unsigned int v) { fprintf(stdout, "%u", v); }
void SerialOut::print(long v) { fprintf(stdout, "%ld", v); }
void SerialOut::print(unsigned long v) { fprintf(stdout, "%lu", v); }
void SerialOut::print(float v, int digits) { fprintf(stdout, "%.*f", digits, static_cast<double>(v)); }
void SerialOut::print(double v, int digits) { fprintf(stdout, "%.*f", digits, v); }

void SerialOut::println() { fputc('\n', stdout); }
void SerialOut::println(const char *s) { fputs(s, stdout); fputc('\n', stdout); }
void SerialOut::println(char c) { fputc(c, stdout); fputc('\n', stdout); }
void SerialOut::println(int v) { print(v); fputc('\n', stdout); }
void SerialOut::println(unsigned int v) { print(v); fputc('\n', stdout); }
void SerialOut::println(long v) { print(v); fputc('\n', stdout); }
void SerialOut::println(unsigned long v) { print(v); fputc('\n', stdout); }
void SerialOut::println(float v, int digits) { print(v, digits); fputc('\n', stdout); }
void SerialOut::println(double v, int digits) { print(v, digits); fputc('\n', stdout); }

void SerialOut::printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

// ============================================================================
// Serial1 (GPS input <- stdin)
// ============================================================================
void SerialIn::begin(unsigned long, uint8_t, int8_t, int8_t) {}

int SerialIn::available() {
    if (!has_pushed_) {
        const int c = getchar();
        if (c == EOF) {
            // End of the NMEA input stream: the firmware's loop() would spin
            // forever waiting for data, so terminate the host process cleanly.
            fflush(stdout);
            exit(0);
        }
        has_pushed_ = true;
        pushed_ = c;
    }
    return 1;
}

int SerialIn::read() {
    if (has_pushed_) {
        has_pushed_ = false;
        return pushed_;
    }
    return getchar();
}

// ============================================================================
// Global instances + Arduino-style entry point
// ============================================================================
SerialOut Serial;
SerialIn Serial1;

void setup();
void loop();

int main() {
    setup();
    for (;;) {
        loop();
    }
}