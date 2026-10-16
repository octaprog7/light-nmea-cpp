/**
 * BasicGPS - Simple GPS data parser
 * Connect GPS module to Serial1 (or Serial for Uno/Nano)
 */

#include <lightnmea.h>

lightnmea::Parser parser;
char serialBuffer[128];
uint8_t bufferIndex = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  
  Serial.println("LightNMEA BasicGPS Example");
  
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_STM32) || defined(ARDUINO_AVR_MEGA2560)
  Serial1.begin(9600);
#elif defined(ESP8266)
  #warning "ESP8266: GPS on Serial (shared with debug output)"
  Serial.begin(9600);
#else
  Serial.begin(9600);
#endif
  
  delay(1000);
}

void loop() {
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_STM32) || defined(ARDUINO_AVR_MEGA2560)
  while (Serial1.available()) {
    char c = Serial1.read();
#else
  while (Serial.available()) {
    char c = Serial.read();
#endif
    
    if (c == '\n' || c == '\r') {
      if (bufferIndex > 0) {
        serialBuffer[bufferIndex] = '\0';
        
        if (parser.parse_line(serialBuffer)) {
          const auto& data = parser.data();
          
          if (data.has_coordinates()) {
            Serial.println("--- GPS Fix ---");
            Serial.print("Latitude:  ");
            Serial.println(data.latitude_deg(), 6);
            Serial.print("Longitude: ");
            Serial.println(data.longitude_deg(), 6);
            
            if (data.has_navigation()) {
              Serial.print("Speed:     ");
              Serial.print(data.speed_kmh());
              Serial.println(" km/h");
            }
            Serial.println();
          }
        }
        bufferIndex = 0;
      }
    } else {
      if (bufferIndex < sizeof(serialBuffer) - 1) {
        serialBuffer[bufferIndex++] = c;
      }
    }
  }
}
