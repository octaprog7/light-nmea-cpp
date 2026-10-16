/**
 * GPSLogger - Log GPS data to SD card in CSV format
 */

#include <lightnmea.h>
#include <SPI.h>
#include <SD.h>

lightnmea::Parser parser;
char serialBuffer[128];
uint8_t bufferIndex = 0;
char csvBuffer[256];

const int chipSelect = 10;
File logFile;
bool sdReady = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  
  Serial.println("LightNMEA GPS Logger");
  
  if (!SD.begin(chipSelect)) {
    Serial.println("SD card failed!");
    sdReady = false;
  } else {
    Serial.println("SD card initialized");
    sdReady = true;
    logFile = SD.open("gpslog.csv", FILE_WRITE);
    if (logFile && logFile.size() == 0) {
      logFile.println("valid,satellites,latitude,longitude,speed,course,altitude,time,date");
      logFile.flush();
    }
    logFile.close();
  }
  
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
            size_t len = lightnmea::to_csv(data, csvBuffer, sizeof(csvBuffer));
            
            if (sdReady) {
              logFile = SD.open("gpslog.csv", FILE_WRITE);
              if (logFile) {
                logFile.println(csvBuffer);
                logFile.flush();
                logFile.close();
              }
            }
            
            Serial.println(csvBuffer);
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
