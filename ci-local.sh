#!/usr/bin/env bash
# ============================================================================
# ci-local.sh — локальный «мини-CI» LightNMEA
# ============================================================================
# Дублирует быстро-воспроизводимые проверки GitHub Actions (ci.yml) ДО пуша.
# Тяжёлые джобы (PlatformIO toolchains, Arduino CLI + esp32 core, Zephyr)
# не воспроизводятся локально — их ловит CI; всё остальное проверяется здесь.
#
# Использование:
#   ./ci-local.sh            # все быстрые проверки (~1-2 мин)
#   ./ci-local.sh --fuzz     # + фаззинг/санитайзеры (как job Quality, ~2-5 мин)
#
# Рекомендуется подключить как pre-push hook (см. .githooks/README):
#   git config core.hooksPath .githooks
# ============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="$ROOT/lib/LightNMEA"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FAILURES=0
step() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }
ok()   { printf '\033[1;32m  OK\033[0m %s\n' "$1"; }
fail() { printf '\033[1;31m FAIL\033[0m %s\n' "$1"; FAILURES=$((FAILURES + 1)); }

CXX="${CXX:-g++}"
CC="${CC:-gcc}"

# ----------------------------------------------------------------------------
step "1/6 CMake Release + тесты (gcc, -Werror)"
# Ловит: ошибки компиляции, MSVC-подобные предупреждения через -Werror,
# провалы тестов. Соответствует job "CMake build & tests (ubuntu-latest)".
if cmake -S "$LIB" -B "$WORK/rel" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-Wall -Wextra -Werror" \
      -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" >"$WORK/rel.log" 2>&1 \
  && cmake --build "$WORK/rel" -j >>"$WORK/rel.log" 2>&1 \
  && ctest --test-dir "$WORK/rel" -C Release >>"$WORK/rel.log" 2>&1; then
  ok "Release: build + ctest"
else
  fail "Release: см. $WORK/rel.log"; tail -30 "$WORK/rel.log"
fi

# ----------------------------------------------------------------------------
step "2/6 CMake Debug + тесты"
# Ловит: баги, скрытые NDEBUG (assert() пуст в Release): unused-переменные
# (MSVC C4189) и неверные ожидания в тестах (случай lon_raw=115166666).
if cmake -S "$LIB" -B "$WORK/dbg" -DCMAKE_BUILD_TYPE=Debug >"$WORK/dbg.log" 2>&1 \
  && cmake --build "$WORK/dbg" -j >>"$WORK/dbg.log" 2>&1 \
  && ctest --test-dir "$WORK/dbg" >>"$WORK/dbg.log" 2>&1; then
  ok "Debug: build + ctest"
else
  fail "Debug: см. $WORK/dbg.log"; tail -30 "$WORK/dbg.log"
fi

# ----------------------------------------------------------------------------
step "3/6 Заголовки: сужение 64->32 бит (clang -Wshorten-64-to-32)"
# Аналог MSVC C4267 (size_t -> uint8_t/int32_t), который не даёт GCC.
# Соответствует job "portability" в ci.yml.
CLANGXX="$(command -v clang++ || command -v clang++-19 || true)"
if [ -n "$CLANGXX" ]; then
  if "$CLANGXX" -fsyntax-only -Wall -Wextra -Werror -Wshorten-64-to-32 \
      -I "$LIB/include" "$LIB/src/lightnmea_c.cpp" >"$WORK/clang.log" 2>&1; then
    ok "clang strict headers"
  else
    fail "clang strict headers: см. $WORK/clang.log"; tail -20 "$WORK/clang.log"
  fi
else
  printf '  SKIP clang++ не найден\n'
fi

# ----------------------------------------------------------------------------
step "4/6 Заголовки: gnu++11 совместимость (ESP32 Arduino default)"
# ESP32 Arduino core по умолчанию компилирует в gnu++11: цифровые разделители
# (C++14) и прочие новшества в заголовках ломают сборку скетчей.
if "$CXX" -std=gnu++11 -fsyntax-only -I "$LIB/include" "$LIB/src/lightnmea_c.cpp" \
    >"$WORK/gnu11.log" 2>&1; then
  ok "gnu++11 syntax (public headers)"
else
  printf '\033[1;33m WARN\033[0m gnu++11: проверьте, нет ли в заголовках фич >C++11 '\
         '(см. $WORK/gnu11.log; не блокирует)\n'
fi

# ----------------------------------------------------------------------------
step "5/6 Arduino include-path симуляция (только src/ в include path)"
# arduino-cli компилирует все .cpp из src/ библиотеки с include path = src/.
# Quoted-include "light_nmea.hpp" из src/ не найдётся — нужен "../include/...".
# Соответствует job "Arduino sketches".
ARD_OK=1
for f in "$LIB"/src/*.cpp; do
  if ! "$CXX" -fsyntax-only -I "$LIB/src" "$f" >>"$WORK/ard.log" 2>&1; then
    ARD_OK=0
  fi
done
if [ "$ARD_OK" -eq 1 ]; then
  ok "src/*.cpp собираются с -I src"
else
  fail "arduino-like include path: см. $WORK/ard.log"; tail -20 "$WORK/ard.log"
fi

# ----------------------------------------------------------------------------
step "6/6 Фаззинг + санитайзеры"
if [ "${1:-}" = "--fuzz" ]; then
  if (cd "$LIB" && FUZZ_RANDOM="${FUZZ_RANDOM:-20000}" \
      ./scripts/run_quality_checks.sh >"$WORK/q.log" 2>&1); then
    ok "run_quality_checks.sh"
  else
    fail "quality: см. $WORK/q.log"; tail -30 "$WORK/q.log"
  fi
else
  printf '  SKIP (запустите ./ci-local.sh --fuzz для полной проверки, как job Quality)\n'
fi

# ----------------------------------------------------------------------------
printf '\n'
if [ "$FAILURES" -eq 0 ]; then
  printf '\033[1;32mВСЕ ПРОВЕРКИ ПРОЙДЕНЫ — можно пушить\033[0m\n'
  exit 0
else
  printf '\033[1;31mПРОВАЛОВ: %d — пушить нельзя, сначала исправьте\033[0m\n' "$FAILURES"
  exit 1
fi
