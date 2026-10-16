# ============================================================================
# LightNMEA FreeRTOS example - CMake build (host, POSIX port)
# ============================================================================
# Собирает пример интеграции LightNMEA с FreeRTOS на хосте (Linux/macOS)
# POSIX-порт FreeRTOS kernel. Не требует железа: данные NMEA читаются
# из файла data/sample.nmea задачей-производителем и передаются
# задаче-потребителю очередью FreeRTOS.
#
# Сборка и запуск:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/freertos_nmea_example        # из каталога examples/freertos
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(freertos_nmea_example LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------------------
# LightNMEA (header-only, ядро не зависит от Arduino/POSIX)
# ----------------------------------------------------------------------------
add_library(light_nmea INTERFACE)
target_include_directories(light_nmea INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# ----------------------------------------------------------------------------
# FreeRTOS kernel (скачивается с GitHub при первой настройке)
# ----------------------------------------------------------------------------
# Использует FetchContent_Populate (без встроенного CMakeLists FreeRTOS-Kernel),
# чтобы собрать ядро с POSIX-портом вручную.
include(FetchContent)
FetchContent_Declare(
  freertos_kernel
  GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
  GIT_TAG        V11.1.0
  GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(freertos_kernel)
if(NOT freertos_kernel_POPULATED)
  FetchContent_Populate(freertos_kernel)
endif()

set(FREERTOS_SRC_DIR  ${freertos_kernel_SOURCE_DIR})
set(FREERTOS_PORT_DIR ${FREERTOS_SRC_DIR}/portable/ThirdParty/GCC/Posix)

add_library(freertos_kernel STATIC
  ${FREERTOS_SRC_DIR}/tasks.c
  ${FREERTOS_SRC_DIR}/queue.c
  ${FREERTOS_SRC_DIR}/list.c
  ${FREERTOS_SRC_DIR}/timers.c
  ${FREERTOS_SRC_DIR}/event_groups.c
  ${FREERTOS_SRC_DIR}/stream_buffer.c
  ${FREERTOS_SRC_DIR}/portable/MemMang/heap_4.c
  ${FREERTOS_PORT_DIR}/port.c
  ${FREERTOS_PORT_DIR}/utils/wait_for_event.c
)

target_include_directories(freertos_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}          # FreeRTOSConfig.h
  ${FREERTOS_SRC_DIR}/include
  ${FREERTOS_PORT_DIR}
)

target_compile_definitions(freertos_kernel PRIVATE _GNU_SOURCE)
target_link_libraries(freertos_kernel PUBLIC pthread)

# ----------------------------------------------------------------------------
# Пример
# ----------------------------------------------------------------------------
add_executable(freertos_nmea_example src/main.cpp)
target_link_libraries(freertos_nmea_example PRIVATE freertos_kernel light_nmea)
target_compile_definitions(freertos_nmea_example PRIVATE _GNU_SOURCE)
