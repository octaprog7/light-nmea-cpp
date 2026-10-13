/*
 * Arduino.h - host (x86) shim for compiling src/main.cpp outside Arduino.
 *
 * Emulates just enough of the Arduino API that src/main.cpp uses:
 *   - Serial  (debug output)  -> stdout
 *   - Serial1 (GPS input)     -> stdin
 *   - millis(), delay(), pinMode(), digitalWrite()
 *   - GPIO constants (OUTPUT/HIGH/LOW), LED_BUILTIN, SERIAL_8N1
 *
 * The platform macro ARDUINO_ARCH_SAMD is supplied by the build script
 * (build_fw.sh) so that src/main.cpp picks a neutral branch with a 4-argument
 * Serial1::begin(baud, config, rx, tx). Nothing platform-specific is used:
 * all pin operations are no-ops and the GPS stream comes from stdin.
 */
#ifndef LIGHT_NMEA_NATIVE_ARDUINO_H
#define LIGHT_NMEA_NATIVE_ARDUINO_H

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <math.h>

// ----------------------------------------------------------------------------
// Pin / mode constants
// ----------------------------------------------------------------------------
#define INPUT           0x0
#define OUTPUT          0x1
#define INPUT_PULLUP    0x2
#define LOW             0x0
#define HIGH            0x1
#define LED_BUILTIN     13

// Serial configuration byte (Arduino-compatible); unused on the host.
#define SERIAL_8N1      0x06

// ----------------------------------------------------------------------------
// Arduino types / math macros (needed by vendored TinyGPSPlus)
// ----------------------------------------------------------------------------
typedef uint8_t byte;

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef TWO_PI
#define TWO_PI (2.0 * PI)
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

// ----------------------------------------------------------------------------
// Core functions (implemented in arduino_native.cpp)
// ----------------------------------------------------------------------------
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);

// ----------------------------------------------------------------------------
// Serial (debug output -> stdout)
// ----------------------------------------------------------------------------
class SerialOut {
public:
    void begin(unsigned long baud);

    // Always "connected": the while (!Serial) wait in setup() must not block.
    explicit operator bool() const { return true; }

    void print(const char *s);
    void print(char c);
    void print(int v);
    void print(unsigned int v);
    void print(long v);
    void print(unsigned long v);
    void print(float v, int digits = 2);
    void print(double v, int digits = 2);

    void println();
    void println(const char *s);
    void println(char c);
    void println(int v);
    void println(unsigned int v);
    void println(long v);
    void println(unsigned long v);
    void println(float v, int digits = 2);
    void println(double v, int digits = 2);

    void printf(const char *fmt, ...);
};

// ----------------------------------------------------------------------------
// Serial1 (GPS input <- stdin)
// ----------------------------------------------------------------------------
class SerialIn {
public:
    // 4-argument signature matching the SAMD/STM32/ESP32 branch of src/main.cpp.
    void begin(unsigned long baud, uint8_t config, int8_t rxPin, int8_t txPin);

    // Returns 1 if a byte is buffered, otherwise tries to read one from stdin.
    // On EOF the process exits cleanly (pipe/model use: cat log.nmea | ./fw).
    int available();

    // Returns the next byte (EOF-aware, but EOF is handled by available()).
    int read();

private:
    bool has_pushed_ = false;
    int pushed_ = -1;
};

// Global Arduino-style instances.
extern SerialOut Serial;
extern SerialIn Serial1;

#endif // LIGHT_NMEA_NATIVE_ARDUINO_H