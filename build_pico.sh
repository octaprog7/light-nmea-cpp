#!/bin/bash
#
# build_pico.sh — скрипт для сборки и управления прошивкой Raspberry Pi Pico
# build_pico.sh - Build and management script for Raspberry Pi Pico firmware
#
# Usage:
#   ./build_pico.sh           # Build firmware
#   ./build_pico.sh upload    # Build and upload to Pico
#   ./build_pico.sh monitor   # Open serial monitor
#   ./build_pico.sh clean     # Clean build cache
#   ./build_pico.sh help      # Show help
#
set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No color

# Путь к PlatformIO CLI (работает для любого пользователя ОС)
PIO="$HOME/.platformio/penv/bin/pio"

# Проверка наличия PlatformIO в ОС
if [ ! -f "$PIO" ]; then
    echo -e "${RED}Error: PlatformIO not found at $PIO${NC}"
    echo "Please install PlatformIO or add it to your PATH"
    exit 1
fi

# В корневой директории проекта PlatformIO или нет?
# Check if we are in the PlatformIO project root
if [ ! -f "platformio.ini" ]; then
    echo -e "${RED}Error: platformio.ini not found in the current directory!${NC}"
    echo "Run this script from the PlatformIO project root."
    exit 1
fi

# Help function
show_help() {
    echo -e "${BLUE}=== Raspberry Pi Pico Build Script ===${NC}"
    echo ""
    echo "Usage:"
    echo "  ./build_pico.sh           Build firmware"
    echo "  ./build_pico.sh upload    Build and upload to board"
    echo "  ./build_pico.sh monitor   Open serial port monitor"
    echo "  ./build_pico.sh clean     Clean temporary build files"
    echo "  ./build_pico.sh help      Show this help"
    echo ""
    echo "Current directory: $(pwd)"
}

# Build function
do_build() {
    echo -e "${BLUE}=== Building firmware ===${NC}"
    "$PIO" run
    echo -e "${GREEN}[OK] Build completed successfully!${NC}"
}

# Upload function
do_upload() {
    echo -e "${BLUE}=== Uploading firmware ===${NC}"
    echo -e "${YELLOW}Make sure Pico is connected and in BOOTSEL mode (if required).${NC}"
    "$PIO" run -t upload
    echo -e "${GREEN}[OK] Firmware uploaded!${NC}"
}

# Monitor function
do_monitor() {
    echo -e "${BLUE}=== Serial Monitor ===${NC}"
    echo -e "Press ${YELLOW}Ctrl+C${NC} to exit."
    echo ""
    "$PIO" device monitor
}

# Clean function
do_clean() {
    echo -e "${YELLOW}=== Cleaning project ===${NC}"
    "$PIO" run -t clean
    echo -e "${GREEN}[OK] Project cleaned.${NC}"
}

# Main logic (argument parsing)
case "${1:-}" in
    upload)
        do_build
        do_upload
        ;;
    monitor)
        do_monitor
        ;;
    clean)
        do_clean
        ;;
    help|--help|-h)
        show_help
        ;;
    "")
        do_build
        ;;
    *)
        echo -e "${RED}Unknown command: $1${NC}"
        echo "Use './build_pico.sh help' for help"
        exit 1
        ;;
esac
