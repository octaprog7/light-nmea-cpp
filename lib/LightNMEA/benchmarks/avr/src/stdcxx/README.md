/*
 * stdcxx/ - минимальные freestanding заменители libstdc++ для сборки
 * бенчмарка (bench_avr.cpp + light_nmea.hpp) на AVR без готовой C++ STL.
 *
 * ПРЕДУПРЕЖДЕНИЕ: это НЕ стандартная библиотека, а узкие заглушки, покрывающие
 * ровно тот API, который использует header-only int-парсер в этом бенче:
 *   - std::string_view (ctor, size, empty, operator[], data, begin/end,
 *                       substr, remove_prefix/suffix, сравнения);
 *   - std::array (value-init, operator[], size, begin/end, data, fill);
 *   - std::type_traits-минимум (is_enum_v, underlying_type_t и базовая инфра);
 *   - cstddef/cstdint (перенаправление на avr-libc).
 *
 * Если будет доступен полноценный AVR-тулчейн с libstdc++ (например,
 * avr-gcc-7.3.0-atmel3.6.1-arduino7), просто установите AVR_GCC_PREFIX
 * на него — build.sh использует заглушки только когда системный avr-g++
 * не находит <string_view>.
 */