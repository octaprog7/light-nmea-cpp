# Восстановление окирпиченного ATmega32L

## Проблема

После изменения `lfuse` с `0xFF` (внешний кварц) на `0xE0` (внутренний осциллятор 8 МГц):
- Чип перестал отвечать через ISP-программатор AVRISP mkII
- Ошибка: `avrdude: device signature = 0x000000` или `Target reverse inserted`
- AVRISP mkII не может синхронизироваться при неправильной частоте

## Причина

AVRISP mkII использует синхронный режим ISP, который требует точного знания частоты чипа. При изменении фьюзов:
1. Чип переключается на внутренний осциллятор 8 МГц / 8 = 1 МГц
2. AVRISP mkII пытается синхронизироваться на старой частоте
3. Синхронизация не удаётся → чип кажется "мёртвым"

## Решения (от простого к сложному)

### Решение 1: USBasp программатор (РЕКОМЕНДУЕТСЯ)

USBasp поддерживает асинхронный ISP и может работать с чипами на любой частоте.

#### Шаг 1: Приобретение USBasp

Стоимость: ~300-500 руб. на AliExpress/Local Markets.

#### Шаг 2: Подключение

```
USBasp → ISP-разъём платы:
  Pin 1 (RESET)  → PB4 (RESET ATmega32, pin 1)
  Pin 2 (VCC)    → VCC (pin 22)
  Pin 3 (MOSI)   → PB5 (MOSI, pin 18)
  Pin 4 (SCK)    → PB7 (SCK, pin 19)
  Pin 5 (MISO)   → PB6 (MISO, pin 17)
  Pin 6 (GND)    → GND (pin 10, 21)
```

**Важно:** USBasp МОЖЕТ питать плату. Убедитесь, что Vtarget = 5V.

#### Шаг 3: Восстановление фьюзов

```bash
# Проверка связи
usbasp-id

# Чтение текущих фьюзов
avrdude -p atmega32 -c usbasp -F -v \
  -U lfuse:r:-:h \
  -U hfuse:r:-:h \
  -U efuse:r:-:h

# Восстановление lfuse для внешнего кварца 8 МГц
avrdude -p atmega32 -c usbasp -F -v \
  -U lfuse:w:0xFF:m

# Или для внутреннего осциллятора 8 МГц (без делителя)
avrdude -p atmega32 -c usbasp -F -v \
  -U lfuse:w:0xE4:m

# Восстановление hfuse по умолчанию
avrdude -p atmega32 -c usbasp -F -v \
  -U hfuse:w:0xD9:m
```

#### Шаг 4: Проверка

```bash
# Должен ответить
avrdude -p atmega32 -c usbasp -F -v

# Прошивка обратно
avrdude -p atmega32 -c usbasp -F -v \
  -U flash:w:bench_avr.hex:i
```

### Решение 2: Высоковольтный программатор (6V на RESET)

Если нет USBasp, можно собрать высоковольтный программатор.

#### Принцип работы

На подачу 12V на RESET принудительно открывает PORTB, позволяя загрузить новую команду ISP.

#### Схема

```
12V источник → RESET через резистор 1K
5V логика    → MOSI, SCK
GND          → GND
```

#### Инструмент

```bash
# Используем avrdude с высоковольтным режимом
avrdude -p atmega32 -c avr109 -B 10 -F -v \
  -U lfuse:w:0xFF:m
```

### Решение 3: Внешний кварц 8 МГц

Если чип поддерживает внешний кварц даже с неправильными фьюзами:

#### Шаг 1: Подключение внешнего кварца

```
XTAL1 (pin 9)  → 8 МГц кварц → GND
XTAL2 (pin 10) → 8 МГц кварц → (через 22 пФ → GND)
```

#### Шаг 2: Попробовать ISP

```bash
# С уменьшенной скоростью
avrdude -p atmega32 -c avrispmkII -P usb -B 20 -F -v
```

### Решение 4: Замена чипа

Самый дешёвый и надёжный вариант (~100-300 руб за чип).

#### Где купить

- AliExpress: ATmega32L-8AU (~150 руб)
- Local Markets: Mirelektronic, Chipdip
- Arduino-магазины: Adafruit, SparkFun

#### Совместимость

- ATmega32L-8AU: Low-voltage, 8 МГц при 5В
- ATmega32-8AU: Стандартное напряжение, 8 МГц
- ATmega32A-8AU: Обновлённая версия, совместима

## Предотвращение проблемы в будущем

### 1. Бэкап фьюзов

```bash
# Сохраните правильные фьюзы
avrdude -p atmega32 -c usbasp -F -v \
  -U lfuse:r:backup_lfuse.hex:h \
  -U hfuse:r:backup_hfuse:h \
  -U efuse:r:backup_efuse:h
```

### 2. Блокировка фьюзов

```bash
# Lock bits для защиты фьюзов
avrdude -p atmega32 -c usbasp -F -v \
  -U lock:w:0xCF:m
```

### 3. Использование безопасных фьюзов

```bash
# Внутренний осциллятор 8 МГц, без делителя
# Безопасен для ISP на частоте до 4 МГц
lfuse = 0xE4
```

## Чеклист восстановления

- [ ] Определить доступный программатор (USBasp/высоковольтный/внешний кварц)
- [ ] Проверить питание платы (5V на VCC)
- [ ] Проверить общую землю (GND программатора и платы)
- [ ] Подключить ISP-кабель правильно (pin 1 = RESET)
- [ ] Попробовать восстановить lfuse через USBasp
- [ ] Проверить связь после восстановления
- [ ] Прошить обратно бенчмарк
- [ ] Сохранить бэкап фьюзов

## Диагностика через USBasp

```bash
# Полный лог
avrdude -p atmega32 -c usbasp -F -vvvv

# Только фьюзы
avrdude -p atmega32 -c usbasp -F -U lfuse:r:-:x

# Сброс всех фьюзов
avrdude -p atmega32 -c usbasp -F \
  -U lfuse:w:0xE4:m \
  -U hfuse:w:0xD9:m \
  -U efuse:w:0xFF:m
```

## FAQ

**В: AVRISP mkII не работает, что делать?**
О: Купите USBasp (~300 руб). Он дешевле и надёжнее для восстановления.

**В: Можно ли восстановить без программатора?**
О: Только внешним кварцем или высоковольтным программатором.

**В: Какой lfuse выбрать для внутреннего осциллятора?**
О: `0xE4` (внутренний 8 МГц, без делителя) — безопасен для ISP.

**В: Чип не отвечает даже через USBasp?**
О: Проверьте питание, землю, подключение ISP. Если не помогло — чип возможно сгорел.

## Ссылки

- [AVR Fuses Calculator](https://www.engbedded.com/fusecalc/)
- [USBasp на AliExpress](https://www.aliexpress.com/wholesale?SearchText=usbasp)
- [ATmega32 Datasheet](https://www.microchip.com/en-us/product/atmega32)