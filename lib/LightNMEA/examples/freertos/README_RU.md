# LightNMEA + FreeRTOS (host, POSIX port)

Пример интеграции библиотеки LightNMEA с FreeRTOS на хосте Linux/macOS с POSIX-портом FreeRTOS kernel.
Железо не требуется.

## Результат работы

- **Задача-производитель** читает NMEA-трек из `data/sample.nmea` и
  отправляет байты в очередь FreeRTOS.
- **Задача-потребитель** реализует абстрактный `light_nmea::Stream` поверх
  очереди (`QueueStream`) и парсит данные классами `NMEAStreamReader` + `Parser`.
- Ядро LightNMEA (`light_nmea.hpp`, `nmea_stream_reader.hpp`,
  `nmea_formatter.hpp`) не зависит от Arduino/POSIX, поэтому работает в любой
  задаче FreeRTOS без изменений.

## Требования

- CMake >= 3.16
- Компилятор C/C++ (GCC/Clang)
- Интернет при первой настройке (FreeRTOS kernel скачивается с GitHub)

## Сборка и запуск

```console
cd lib/LightNMEA/examples/freertos
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/freertos_nmea_example
```

Ожидаемый вывод (фрагмент):

```
LightNMEA FreeRTOS example: starting scheduler
Producer: sent 342 bytes to queue
Fix: Yes
satellites: 8
latitude: 48.117300
longitude: 11.516667
...
Consumer: done. processed=6 aborted=0 anti_spam_dropped=0
```

## Как перенести код на реальное железо?

1. Замените `QueueStream` на реализацию `Stream` поверх вашего драйвера UART
   (или оставьте очередь, если поток данных приходит из другой задачи/ISR).
2. Используйте свой `FreeRTOSConfig.h` и порт FreeRTOS для вашей(!) платформы вместо порта POSIX.
3. `get_time_ms()` замените на функцию времени на вашей(!) платформе, например, `xTaskGetTickCount()`.
