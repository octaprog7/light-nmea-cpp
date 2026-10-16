# LightNMEA + Zephyr

Пример интеграции библиотеки LightNMEA с Zephyr RTOS. Приложение читает поток 
данных NMEA из UART с помощью API `uart_pipe`, копит данные в кольцевом
буфере и парсит их в главном цикле библиотекой LightNMEA (`NMEAStreamReader` +
`Parser`).

Ядро LightNMEA (`light_nmea.hpp`, `nmea_stream_reader.hpp`,
`nmea_formatter.hpp`) не зависит от Arduino/POSIX, поэтому работает в Zephyr
без изменений.

## Требования

- [Zephyr RTOS](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
  (west + Zephyr SDK), версия 3.6+.
- Для `native_sim` дополнительно нужны 32-битные библиотеки хоста:
  `sudo apt install gcc-multilib g++-multilib`.

## Сборка и запуск

### native_sim (без железа)

```console
cd lib/LightNMEA/examples/zephyr
west build -b native_sim -d build/native_sim
./build/native_sim/zephyr/zephyr.exe
```

На native_sim UART0 подключён к stdin/stdout: пишите строки NMEA в
терминал (например, `$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A`) и приложение выведет распознанные координаты.

### Плата (проверено: nRF52840 DK)

```console
cd lib/LightNMEA/examples/zephyr
west build -b nrf52840dk/nrf52840 -d build/nrf52840dk
west flash -d build/nrf52840dk
```

Overlay для nRF52840 DK лежит в `boards/nrf52840dk_nrf52840.overlay`
(UART0, pins P0.06 TX / P0.08 RX, 115200 бод). Подключите GPS-модуль к
UART0 и следите за выводом в `west build -t monitor` (или свой терминал).

Для другой платы создайте свой overlay в `boards/` и настройте
`chosen { zephyr,uart-pipe = &uartX; };` на нужный UART.

## Описание работы

1. `uart_pipe_register(buf, len, pipe_handler)` — регистрирует буфер и
   обработчик, который получает данные из UART (устройство выбирается из devicetree `chosen: zephyr,uart-pipe`).
2. `pipe_handler` пишет байты в кольцевой буфер.
3. `RingStream` реализует абстрактный `light_nmea::Stream::read()` поверх кольцевого буфера.
4. `reader.read_available(parser, stream, get_time_ms)` формирует пакеты NMEA и передаёт их парсеру; 
5. результат выводит `printk`.

## Конфигурация (prj.conf)

- `CONFIG_UART_PIPE=y` — API uart_pipe.
- `CONFIG_CPP=y`, `CONFIG_STD_CPP17=y`, `CONFIG_REQUIRES_FULL_LIBCPP=y` —
  C++17 и полная libstdc++ (нужна для `std::string_view::substr` и
  C++ runtime).
