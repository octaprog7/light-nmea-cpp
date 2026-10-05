/**
 * GPSToJSON - Parse GPS and output as JSON
 */

#include <lightnmea.h>

lightnmea::Parser parser;
char serialBuffer[128];
uint8_t bufferIndex = 0;
char jsonBuffer[256];

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  
  Serial.println("LightNMEA GPS to JSON");
  
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_AVR_MEGA2560)
  Serial1.begin(9600);
#else
  Serial.begin(9600);
#endif
  
  delay(1000);
}

void loop() {
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_AVR_MEGA2560)
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
            size_t len = lightnmea::to_json(data, jsonBuffer, sizeof(jsonBuffer));
            Serial.println(jsonBuffer);
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
