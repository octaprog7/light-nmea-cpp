#!/bin/bash

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== LightNMEA Benchmarks Build Script ===${NC}"
echo ""

# Переход в директорию скрипта
cd "$(dirname "$0")"

# Создание директории build
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning old build...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Конфигурация CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# Сборка
echo -e "${YELLOW}Building benchmarks...${NC}"
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}=== Build successful! ===${NC}"
echo ""
echo -e "Executables:"
ls -lh bench_parser bench_nav_gen
echo ""
echo -e "${YELLOW}To run benchmarks:${NC}"
echo "  ./bench_parser"
echo "  ./bench_nav_gen"
