#!/bin/bash
# ============================================================================
# build_gnss_dashboard.sh - сборка GNSS-дашборда (TUI) для Linux (Debian/Ubuntu).
# Copyright (C) 2026 Roman Shevchik
#
# Собирает examples/gnss_dashboard.cpp + examples/dash_utils.hpp
# (dash_utils.hpp - заголовочный модуль без зависимостей от curses)
# в исполняемый файл gnss_dashboard вызовом CMake-цель gnss_dashboard
# (определена в корневом CMakeLists.txt).
#
# Требования:
#   * build-essential, cmake
#   * libncurses-dev (ncursesw) - только для дашборда:
#       sudo apt install build-essential cmake libncurses-dev
#
# Использование:
#   ./build_gnss_dashboard.sh               # Release, out/build/release
#   ./build_gnss_dashboard.sh Debug         # тип сборки (Debug/Release/RelWithDebInfo/...)
#   ./build_gnss_dashboard.sh --clean       # очистить каталог сборки перед сборкой
#   BUILD_DIR=/tmp/gnss ./build_gnss_dashboard.sh   # свой каталог сборки
#
# Результат: out/build/release/gnss_dashboard
# ============================================================================

set -u

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Каталог скрипта и корень репозитория (LightNMEA)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ============================================================================
# Парсинг аргументов
# ============================================================================
BUILD_TYPE="Release"
DO_CLEAN=0

for arg in "$@"; do
    case "$arg" in
        --clean)
            DO_CLEAN=1
            ;;
        *)
            # Первый не-флаг аргумент - тип сборки CMake
            if [ "$BUILD_TYPE" = "Release" ] && [ -n "$arg" ]; then
                BUILD_TYPE="$arg"
            fi
            ;;
    esac
done

# Каталог сборки по умолчанию: out/build/<тип> (lowercase)
BUILD_TYPE_LOWER="$(printf '%s' "$BUILD_TYPE" | tr 'A-Z' 'a-z')"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/out/build/$BUILD_TYPE_LOWER}"

echo -e "${GREEN}=== GNSS Dashboard Build Script ===${NC}"
echo -e "Source dir : $REPO_ROOT"
echo -e "Build type : $BUILD_TYPE"
echo -e "Build dir  : $BUILD_DIR"
echo ""

# ============================================================================
# Проверка инструментов
# ============================================================================
if ! command -v cmake >/dev/null 2>&1; then
    echo -e "${RED}Error: cmake not found!${NC}"
    echo "Install it:"
    echo "  sudo apt install build-essential cmake"
    exit 1
fi

# ncursesw нужен только для gnss_dashboard; если его нет, CMake просто
# пропустит цель. Предупреждаем заранее, чтобы не было сюрпризов.
NCURSES_HINT=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists ncursesw 2>/dev/null; then
    : # ncursesw найден в pkg-config
elif [ -f /usr/include/ncursesw/curses.h ] || [ -f /usr/local/include/ncursesw/curses.h ]; then
    : # ncursesw найден в заголовочном файле
else
    NCURSES_HINT=" (ncursesw not found!)"
fi
if [ -n "$NCURSES_HINT" ]; then
    echo -e "${YELLOW}Warning: ncursesw not found$NCURSES_HINT${NC}"
    echo "The gnss_dashboard target will be SKIPPED by CMake."
    echo "Install it and rebuild:"
    echo "  sudo apt install libncurses-dev"
    echo ""
fi

# ============================================================================
# Очистка каталога сборки (по запросу)
# ============================================================================
if [ "$DO_CLEAN" = "1" ] && [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning old build...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# ============================================================================
# Конфигурация CMake (инкрементальная: повторный запуск безболезненен)
# ============================================================================
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# ============================================================================
# Сборка цели gnss_dashboard
# ============================================================================
echo -e "${YELLOW}Building gnss_dashboard...${NC}"
cmake --build "$BUILD_DIR" --target gnss_dashboard -j"$(nproc)"

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

BINARY="$BUILD_DIR/gnss_dashboard"
if [ ! -x "$BINARY" ]; then
    echo -e "${RED}Error: $BINARY was not produced!${NC}"
    echo "Check the CMake output above: target 'gnss_dashboard' may have been"
    echo "skipped because ncursesw was not found."
    exit 1
fi

echo ""
echo -e "${GREEN}=== Build successful! ===${NC}"
echo -e "Executable: $(ls -lh "$BINARY" | awk '{print $5}')  $BINARY"
echo ""
echo -e "${YELLOW}To run:${NC}"
echo "  $BINARY                       # автодетект USB-CDC @ 115200"
echo "  $BINARY /dev/ttyACM0          # USB-CDC всегда 115200"
echo "  $BINARY /dev/ttyUSB0 9600     # USB-UART адаптер"
echo "Управление: q / Q / ESC - выход. Ошибки - в mcu_debug.log, данные - в gnss_log.csv."