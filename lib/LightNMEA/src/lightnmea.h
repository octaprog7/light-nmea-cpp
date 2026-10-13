/**
 * @file lightnmea.h
 * @brief Arduino IDE compatibility shim (forwarding header).
 *
 * Arduino IDE (Library 1.5 format) adds only the library's `src/` directory
 * to the include path, so the public headers must be reachable from there.
 * This forwarding header re-exports the real umbrella header from `include/`.
 *
 * PlatformIO and CMake use `include/` directly and are not affected.
 */
#pragma once

#include "../include/lightnmea.h"