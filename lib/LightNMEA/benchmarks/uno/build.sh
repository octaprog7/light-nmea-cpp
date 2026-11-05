#!/usr/bin/env bash
#
# build.sh - сборка и прошивка AVR-бенчмарка LightNMEA на Arduino UNO R3
# (ATmega328P @ 16 MHz + CH340G USB-UART).
#
# Переписано с ../avr/ (ATmega32 + AVRISP mkII) под штатную плату UNO:
#   - прошивка через on-board Optiboot bootloader по UART (-c arduino, CH340G);
#   - фьюзы НИКОГДА не пишутся (только чтение в действии check) -> окирпичить
#     чип невозможно, в отличие от ISP-сценария из ../avr/brick-recovery.md;
#   - авто-ресет по DTR: открытие порта перезапускает бенч, программатор не нужен.
#
# Использование:
#   ./build.sh                 собрать (BENCH_PACKET_COUNT=10000, -O2)
#   ./build.sh check           проверить плату (сигнатура, фьюзы read-only)
#   ./build.sh flash           собрать + прошить через загрузчик
#   ./build.sh monitor         монитор порта (DTR-ресет перезапускает бенч)
#   ./build.sh run             прошить + снять вывод бенча одной командой
#   BENCH_PACKET_COUNT=30000 ./build.sh
#   OPT=-Os ./build.sh         оптимизация по размеру
#   PORT=/dev/ttyUSB1 ./build.sh flash
#   ./build.sh clean           очистить артефакты
#   ./build.sh help            справка
#
# Требуется: avr-gcc >= 9 (C++17), avr-libc, avrdude >= 6.3 (arduino programmer).
# Метрика: pkt/s, ns/pkt, cycles/pkt, pkt/s на МГц (best-of-N), как на 32-бит платах.
set -euo pipefail
cd "$(dirname "$0")"

MCU="${MCU:-atmega328p}"
F_CPU="${F_CPU:-16000000}"        # кварц на плате UNO R3 = 16 МГц
OPT="${OPT:--O2}"
COUNT="${BENCH_PACKET_COUNT:-10000}"
UPLOAD_BAUD="${UPLOAD_BAUD:-115200}"   # Optiboot на UNO R3 всегда 115200
PORT="${PORT:-/dev/ttyUSB0}"           # CH340G
BENCH="${BENCH:-uno}"

# Последовательный монитор
PY3_PIO="$HOME/.platformio/penv/bin/python3"

# Логика выбора бод(бит/c) должна совпадать с uart_cfg::pick_baud() в bench_uno.cpp.
pick_baud() {
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
# ../avr/src содержит minmea_compat.h (struct timespec/timegm для avr-libc)
# и freestanding-заглушки stdcxx.
INC+=(-I../avr/src)
DEF=(-DF_CPU="${F_CPU}UL" -DBENCH_PACKET_COUNT="${COUNT}UL")
CC_FLAGS=(-mmcu="$MCU" -Wall -Wextra -ffunction-sections -fdata-sections)
CXX_FLAGS=("${CC_FLAGS[@]}" -fno-exceptions -fno-rtti)

# Если C++-компилятор не имеет libstdc++ (Debian gcc-avr), подключаем
# локальные freestanding-заглушки src/stdcxx из ../avr/ (см. ../avr/README.md).
CXX_STUB_INC=()
if ! printf '#include <string_view>\n' | avr-g++ -std=gnu++17 -mmcu="$MCU" -x c++ -fsyntax-only - >/dev/null 2>&1; then
    echo "Note: no <string_view> in toolchain, using freestanding shims (../avr/src/stdcxx)"
    CXX_STUB_INC=(-I../avr/src/stdcxx)
fi

ELF="build/bench_${BENCH}.elf"
HEX="build/bench_${BENCH}.hex"

show_help() {
    awk 'NR > 1 { if ($0 ~ /^#/) { sub(/^# /, ""); print } else exit }' "$0"
}

# --- сборка ---------------------------------------------------------------
build() {
    mkdir -p build
    echo "=== Building UNO benchmark: $MCU @ ${F_CPU} Hz, $OPT, ${COUNT} pkts/pass, uart $BAUD ==="

    avr-gcc "${CC_FLAGS[@]}" -std=gnu11 -DMINMEA_INCLUDE_COMPAT \
        "${DEF[@]}" "${INC[@]}" "$OPT" \
        -c "$FW_LIB/minmea/src/minmea.c" -o build/minmea.o

    avr-g++ "${CXX_FLAGS[@]}" -std=gnu++17 "${DEF[@]}" "${CXX_STUB_INC[@]}" "${INC[@]}" "$OPT" \
        -c "$FW_LIB/TinyGPSPlus/src/TinyGPS++.cpp" -o build/tinygps.o

    avr-g++ "${CXX_FLAGS[@]}" -std=gnu++17 "${DEF[@]}" "${CXX_STUB_INC[@]}" "${INC[@]}" "$OPT" \
        -c "$SRC/bench_uno.cpp" -o build/bench_uno.o

    # -nodefaultlibs: в сборках avr-g++ без libstdc++ иначе драйвер требует
    # -lstdc++; наш код header-only, без исключений/RTTI. Группа библиотек
    # (libc/libgcc/libm) идёт ПОСЛЕ объектов, иначе коллектор не увидит
    # undefined-ссылки.
    avr-g++ -mmcu="$MCU" -Wl,--gc-sections -nodefaultlibs \
        build/minmea.o build/tinygps.o build/bench_uno.o \
        -Wl,--start-group -lc -lgcc -lm -Wl,--end-group \
        -o "$ELF"

    avr-objcopy -O ihex -R .eeprom "$ELF" "$HEX"
    avr-size --mcu="$MCU" "$ELF"

    # Контроль лимитов: ATmega328P = 32 КБ flash, 2 КБ SRAM.
    FLASH_LIMIT=$(( 32768 * 103 / 100 ))   # +3% запас на Optiboot-ограничения
    RAM_LIMIT=$(( 2048 * 103 / 100 ))
    FLASH_SIZE=$(avr-size "$ELF" | awk 'NR==2 {print $1 + $2}')
    DATA_SIZE=$(avr-size "$ELF" | awk 'NR==2 {print $2 + $3}')
    if [ "$FLASH_SIZE" -gt "$FLASH_LIMIT" ]; then
        echo "Error: flash $FLASH_SIZE B > $FLASH_LIMIT B (try OPT=-Os)" >&2
        exit 1
    fi
    if [ "$DATA_SIZE" -gt "$RAM_LIMIT" ]; then
        echo "Error: SRAM $DATA_SIZE B > $RAM_LIMIT B" >&2
        exit 1
    fi
    echo "=== OK: flash $FLASH_SIZE B, SRAM $DATA_SIZE B ==="
}

# --- проверка платы (read-only, фьюзы НЕ трогаем) ---------------------------
check() {
    echo "=== Checking board on $PORT (Optiboot, read-only) ==="
    avrdude -p "$MCU" -c arduino -P "$PORT" -b "$UPLOAD_BAUD" \
        -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h
    echo "=== OK: board is alive (fuses were read, not written) ==="
}

# --- прошивка через загрузчик -----------------------------------------------
flash() {
    build
    echo "=== Flashing via Optiboot bootloader ($PORT @ $UPLOAD_BAUD, fuses untouched) ==="
    avrdude -p "$MCU" -c arduino -P "$PORT" -b "$UPLOAD_BAUD" \
        -D \
        -U flash:w:"$HEX":i
    echo "=== Flashed. Bench starts immediately after upload (Optiboot auto-jump) ==="
}

# --- монитор ----------------------------------------------------------------
monitor() {
    echo "=== Monitoring $PORT @ $BAUD (opening the port asserts DTR -> bench re-runs) ==="
    if [ -e "$PY3_PIO" ]; then
        "$PY3_PIO" -m serial.tools.miniterm "$PORT" "$BAUD" --raw
    else
        exec 3<>"$PORT"
        stty -F "$PORT" "$BAUD" raw -echo
        cat <&3
    fi
}

# --- прошить и снять вывод одной командой ------------------------------------
run() {
    flash
    echo "=== Reading bench output from $PORT @ $BAUD (10 s) ==="
    sleep 0.3
    if [ -e "$PY3_PIO" ]; then
        "$PY3_PIO" -m serial.tools.miniterm "$PORT" "$BAUD" --raw --quiet < /dev/null &
        PID=$!
        sleep 10
        kill $PID 2>/dev/null || true
        wait $PID 2>/dev/null || true
    else
        exec 3<>"$PORT"
        stty -F "$PORT" "$BAUD" raw -echo
        timeout 10 cat <&3 || true
        exec 3<&-
    fi
}

case "${1:-build}" in
    build)   build ;;
    check)   check ;;
    flash)   flash ;;
    monitor) monitor ;;
    run)     run ;;
    clean)   rm -rf build ;;
    help|-h|--help) show_help ;;
    *) echo "Error: unknown action '${1:-}' (build | check | flash | monitor | run | clean | help)" >&2; exit 1 ;;
esac
