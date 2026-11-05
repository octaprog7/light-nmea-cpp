#!/usr/bin/env bash
#
# build.sh - сборка AVR-бенчмарков LightNMEA
# на классическом 8-битном AVR (ATmega32L-8AU, avr5) через AVRISP mkII.
#
# Bare-metal harness: без Arduino-ядра, USART0 (PD1 TX),
# автовыбор бода по частоте (при 8 МГц — 38400, при 16 МГц — 115200).
#
# Варианты бенчмарков (BENCH=avr|simple, по умолчанию avr):
#   avr   - полный сравнительный (LightNMEA + minmea + TinyGPSPlus)
#   simple- только LightNMEA, метрика pkt/s
#
# Использование:
#   ./build.sh                    собрать (BENCH=avr, F_CPU=8000000, -O2, 10000 пакетов/проход)
#   BENCH=simple ./build.sh       собрать простой бенч (только LightNMEA)
#   F_CPU=16000000 ./build.sh     собрать под 16 МГц
#   BENCH_PACKET_COUNT=30000 ./build.sh
#   OPT=-Os ./build.sh            оптимизация по размеру
#   ./build.sh flash              собрать + прошить HEX через AVRISP mkII
#   ./build.sh fuses              прочитать сигнатуру, lfuse/hfuse (read-only)
#   ./build.sh monitor            последовательный монитор (USB-UART на PD0/PD1)
#   ./build.sh clean              очистить артефакты
#   ./build.sh help               справка
#
# Требуется: avr-gcc >= 9 (C++17), avr-libc, avrdude >= 6 (поддержка avrispmkII).
# Метрика: pkt/s, ns/pkt, cycles/pkt, pkt/s на МГц (best-of-N), как на 32-бит платах.
set -euo pipefail
cd "$(dirname "$0")"

MCU="${MCU:-atmega32}"
F_CPU="${F_CPU:-8000000}"
OPT="${OPT:--O2}"
COUNT="${BENCH_PACKET_COUNT:-10000}"
PROG="${PROG:-avrispmkII}"
BAUD_ARG="${BAUD:-}"
BENCH="${BENCH:-avr}"

# Последовательный монитор
PY3_PIO="$HOME/.platformio/penv/bin/python3"
SERIAL_PORT="${SERIAL_PORT:-/dev/ttyUSB0}"

# Логика выбора бода должна совпадать с uart_cfg::pick_baud() в bench_avr.cpp.
pick_baud() {
    if [ -n "$BAUD_ARG" ]; then
        echo "$BAUD_ARG"; return
    fi
    case "$F_CPU" in
        7372800|12000000|14745600|16000000) echo 115200 ;;
        8000000)                            echo 38400  ;;
        20000000)                           echo 57600  ;;
        *)                                  echo 38400  ;;
    esac
}
BAUD="$(pick_baud)"

FW_LIB="../firmware/lib"
SRC="src"
INC=(-Isrc -I../../include -I"$FW_LIB/minmea/include" -I"$FW_LIB/TinyGPSPlus/include")
DEF=(-DF_CPU="${F_CPU}UL" -DBENCH_PACKET_COUNT="${COUNT}UL")
CC_FLAGS=(-mmcu="$MCU" -Wall -Wextra -ffunction-sections -fdata-sections)
CXX_FLAGS=("${CC_FLAGS[@]}" -fno-exceptions -fno-rtti)

# --- выбор компилятора -------------------------------------------------------
# Полноценный AVR-тулчейн (с libstdc++) можно задать через AVR_GCC_PREFIX,
# например: AVR_GCC_PREFIX=$HOME/avr-gcc-7.3.0-atmel3.6.1/bin ./build.sh
# Иначе используются системные avr-gcc/avr-g++.
if [ -n "${AVR_GCC_PREFIX:-}" ]; then
    CC="$AVR_GCC_PREFIX/avr-gcc"
    CXX="$AVR_GCC_PREFIX/avr-g++"
else
    CC=avr-gcc
    CXX=avr-g++
fi

# Если C++-компилятор не имеет libstdc++ (Debian gcc-avr, PlatformIO toolchain),
# подключаем локальные freestanding-заглушки src/stdcxx (см. README.md).
CXX_STUB_INC=()
if ! printf '#include <string_view>\n' | "$CXX" -std=gnu++17 -mmcu="$MCU" -x c++ -fsyntax-only - >/dev/null 2>&1; then
    echo "Note: no <string_view> in toolchain, using freestanding shims (src/stdcxx)"
    CXX_STUB_INC=(-Isrc/stdcxx)
fi

OBJCOPY=avr-objcopy
SIZE=avr-size
[ -n "${AVR_GCC_PREFIX:-}" ] && OBJCOPY="$AVR_GCC_PREFIX/avr-objcopy" && SIZE="$AVR_GCC_PREFIX/avr-size"

show_help() {
    awk 'NR > 1 { if ($0 ~ /^#/) { sub(/^# /, ""); print } else exit }' "$0"
}

# --- сборка ---------------------------------------------------------------
build() {
    mkdir -p build
    echo "=== Building AVR benchmark ($BENCH): $MCU @ ${F_CPU} Hz, $OPT, ${COUNT} pkts/pass, baud $BAUD ==="

    if [ "$BENCH" = "simple" ]; then
        # Простой бенч: только LightNMEA (header-only), без minmea/TinyGPSPlus.
        "$CXX" "${CXX_FLAGS[@]}" -std=gnu++17 "${DEF[@]}" "${CXX_STUB_INC[@]}" "${INC[@]}" "$OPT" \
            -c "$SRC/bench_simple.cpp" -o build/bench_simple.o

        "$CXX" -mmcu="$MCU" -Wl,--gc-sections -nodefaultlibs \
            build/bench_simple.o \
            -Wl,--start-group -lc -lgcc -lm -Wl,--end-group \
            -o build/bench_simple.elf

        "$OBJCOPY" -O ihex -R .eeprom build/bench_simple.elf build/bench_simple.hex
        "$SIZE" --mcu="$MCU" build/bench_simple.elf

        # Контроль лимитов: ATmega32 = 32 КБ flash, 2 КБ SRAM.
        FLASH_LIMIT=$(( 32768 * 103 / 100 ))
        RAM_LIMIT=$(( 2048 * 103 / 100 ))
        FLASH_SIZE=$("$SIZE" build/bench_simple.elf | awk 'NR==2 {print $1 + $2}')
        DATA_SIZE=$("$SIZE" build/bench_simple.elf | awk 'NR==2 {print $2 + $3}')
        if [ "$FLASH_SIZE" -gt "$FLASH_LIMIT" ]; then
            echo "Error: flash $FLASH_SIZE B > $FLASH_LIMIT B (try OPT=-Os)" >&2
            exit 1
        fi
        if [ "$DATA_SIZE" -gt "$RAM_LIMIT" ]; then
            echo "Error: SRAM $DATA_SIZE B > $RAM_LIMIT B" >&2
            exit 1
        fi
        echo "=== OK: flash $FLASH_SIZE B, SRAM $DATA_SIZE B ==="
    else
        # Полный сравнительный бенч: LightNMEA + minmea + TinyGPSPlus.
        "$CC" "${CC_FLAGS[@]}" -std=gnu11 -DMINMEA_INCLUDE_COMPAT "${DEF[@]}" "${INC[@]}" "$OPT" \
            -c "$FW_LIB/minmea/src/minmea.c" -o build/minmea.o

        "$CXX" "${CXX_FLAGS[@]}" -std=gnu++17 "${DEF[@]}" "${CXX_STUB_INC[@]}" "${INC[@]}" "$OPT" \
            -c "$FW_LIB/TinyGPSPlus/src/TinyGPS++.cpp" -o build/tinygps.o

        "$CXX" "${CXX_FLAGS[@]}" -std=gnu++17 "${DEF[@]}" "${CXX_STUB_INC[@]}" "${INC[@]}" "$OPT" \
            -c "$SRC/bench_avr.cpp" -o build/bench_avr.o

        # -nodefaultlibs: в сборках avr-g++ без libstdc++ иначе драйвер требует -lstdc++;
        # наш код header-only, без исключений/RTTI. Группа библиотек идёт ПОСЛЕ
        # объектов (libc/libgcc/libm), иначе коллектор не увидит undefined-ссылки.
        "$CXX" -mmcu="$MCU" -Wl,--gc-sections -nodefaultlibs \
            build/minmea.o build/tinygps.o build/bench_avr.o \
            -Wl,--start-group -lc -lgcc -lm -Wl,--end-group \
            -o build/bench_avr.elf

        "$OBJCOPY" -O ihex -R .eeprom build/bench_avr.elf build/bench_avr.hex
        "$SIZE" --mcu="$MCU" build/bench_avr.elf

        # Контроль лимитов: ATmega32 = 32 КБ flash, 2 КБ SRAM.
        FLASH_LIMIT=$(( 32768 * 103 / 100 ))
        RAM_LIMIT=$(( 2048 * 103 / 100 ))
        FLASH_SIZE=$("$SIZE" build/bench_avr.elf | awk 'NR==2 {print $1 + $2}')
        DATA_SIZE=$("$SIZE" build/bench_avr.elf | awk 'NR==2 {print $2 + $3}')
        if [ "$FLASH_SIZE" -gt "$FLASH_LIMIT" ]; then
            echo "Error: flash $FLASH_SIZE B > $FLASH_LIMIT B (try OPT=-Os)" >&2
            exit 1
        fi
        if [ "$DATA_SIZE" -gt "$RAM_LIMIT" ]; then
            echo "Error: SRAM $DATA_SIZE B > $RAM_LIMIT B" >&2
            exit 1
        fi
        echo "=== OK: flash $FLASH_SIZE B, SRAM $DATA_SIZE B ==="
    fi
}

# --- прошивка --------------------------------------------------------------
flash() {
    build
    echo "=== Flashing via AVRISP mkII (flash only, fuses untouched) ==="
    if [ "$BENCH" = "simple" ]; then
        avrdude -p "$MCU" -c "$PROG" -P usb -B 4 \
            -U flash:w:build/bench_simple.hex:i
    else
        avrdude -p "$MCU" -c "$PROG" -P usb -B 4 \
            -U flash:w:build/bench_avr.hex:i
    fi
}

# --- чтение сигнатуры и фьюзов (read-only) ----------------------------------
fuses() {
    echo "=== Signature ==="
    avrdude -p "$MCU" -c "$PROG" -P usb -B 8 -v | grep -E "signature|device" || true
    echo "=== lfuse/hfuse (read-only, не трогаем) ==="
    avrdude -p "$MCU" -c "$PROG" -P usb -B 8 \
        -U lfuse:r:-:h \
        -U hfuse:r:-:h
}

# --- монитор ---------------------------------------------------------------
monitor() {
    echo "=== Monitoring $SERIAL_PORT @ $BAUD (reboot the board to re-run bench) ==="
    if [ -e "$PY3_PIO" ]; then
        "$PY3_PIO" -m serial.tools.miniterm "$SERIAL_PORT" "$BAUD" --raw
    else
        exec 3<>"$SERIAL_PORT"
        stty -F "$SERIAL_PORT" "$BAUD" raw -echo
        cat <&3
    fi
}

case "${1:-build}" in
    build)  build ;;
    flash)  flash ;;
    fuses)  fuses ;;
    monitor) monitor ;;
    clean)  rm -rf build ;;
    help|-h|--help) show_help ;;
    *) echo "Error: unknown action '${1:-}' (build | flash | fuses | monitor | clean | help)" >&2; exit 1 ;;
esac