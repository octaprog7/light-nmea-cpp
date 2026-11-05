/*
 * minmea_compat.h - совместимость minmea.c с avr-libc (bare-metal AVR).
 *
 * Подключается через штатный хук minmea: -DMINMEA_INCLUDE_COMPAT при сборке
 * minmea.c. В avr-libc нет struct timespec и timegm(); функция minmea_gettime()
 * в бенче НЕ используется и удаляется линкером (--gc-sections), но сам TU
 * должен скомпилироваться. mktime эквивалентен timegm при UTC (на плате TZ=UTC
 * не применяется, mktime трактует tm как локальное время = UTC).
 */
#ifndef MINMEA_COMPAT_H
#define MINMEA_COMPAT_H

#include <time.h>

#ifndef timegm
#define timegm mktime
#endif

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#endif /* MINMEA_COMPAT_H */