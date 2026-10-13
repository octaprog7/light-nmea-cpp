#!/bin/bash
#
# run_quality_checks.sh - full quality check suite for LightNMEA.
#
# What is checked:
#   1. Clean compilation of all tests: -std=c++17 -Wall -Wextra -Wpedantic
#      (0 warnings = C++17 compatibility confirmed).
#   2. Unit tests: test_parser, test_stream_reader, test_formatter.
#   3. Fuzz test (tests/fuzz_parser.cpp): deterministic + random mutations of
#      packets, coordinate invariants, recovery after garbage.
#   4. Sanitizers: the same suite under ASan + UBSan (+ leaks) - catches
#      out-of-bounds access, use-after-free, int overflows, division by zero.
#   5. -fno-exceptions -fno-rtti: guaranteed to work without exceptions/RTTI.
#   6. valgrind (if installed): memcheck on a reduced fuzz run.
#
# Usage:
#   ./run_quality_checks.sh                # all checks (default)
#   ./run_quality_checks.sh unit           # unit tests only (release)
#   ./run_quality_checks.sh fuzz [N]       # fuzz test, N random mutations
#   ./run_quality_checks.sh sanitize       # unit + fuzz under ASan/UBSan
#   ./run_quality_checks.sh noexceptions   # build without exceptions/RTTI
#   ./run_quality_checks.sh valgrind       # memcheck (if installed)
#   ./run_quality_checks.sh clean          # remove build-checks/
#   ./run_quality_checks.sh help
#
# Exit code: 0 - all checks passed; 1 - at least one failed.
#
set -u

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
cd "$(dirname "$0")/.."          # теперь мы в lib/LightNMEA

CC="${CXX:-g++}"
INC="-Iinclude"
BUILD_DIR="build-checks"
mkdir -p "$BUILD_DIR"

FUZZ_RANDOM="${FUZZ_RANDOM:-1000000}"        # случайных мутаций в release-фазе
FUZZ_RANDOM_SAN="${FUZZ_RANDOM_SAN:-200000}" # в sanitize-фазе (медленнее)

UNIT_TESTS="tests/test_parser.cpp tests/test_stream_reader.cpp tests/test_formatter.cpp"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass=0
fail_total=0

ok()   { echo -e "${GREEN}[OK]${NC} $1"; pass=$((pass+1)); }
bad()  { echo -e "${RED}[FAIL]${NC} $1"; fail_total=$((fail_total+1)); }
info() { echo -e "${YELLOW}[..]${NC} $1"; }

# ---------------------------------------------------------------------------
# Вспомогательные функции
# ---------------------------------------------------------------------------

# Компиляция набора исходников в один бинарный файл.
# build_one <имя-бинаря> <флаги> <исходники...>
build_one() {
    local name="$1"; shift
    local flags="$1"; shift
    # shellcheck disable=SC2086
    $CC $flags $INC "$@" -o "$BUILD_DIR/$name" 2>"$BUILD_DIR/$name.build.log"
    local rc=$?
    if [ $rc -ne 0 ]; then
        echo "--- build log for $name ---"
        cat "$BUILD_DIR/$name.build.log"
        return 1
    fi
    # Предупреждения в логе = нарушение "чистой" компиляции
    if [ -s "$BUILD_DIR/$name.build.log" ]; then
        echo "--- warnings for $name ---"
        cat "$BUILD_DIR/$name.build.log"
        return 2
    fi
    return 0
}

# Прогон бинарного файла; код возврата 0 = успех.
run_bin() {
    local name="$1"; shift
    "$BUILD_DIR/$name" "$@" >"$BUILD_DIR/$name.run.log" 2>&1
    local rc=$?
    if [ $rc -ne 0 ]; then
        tail -20 "$BUILD_DIR/$name.run.log"
        return 1
    fi
    return 0
}

# ---------------------------------------------------------------------------
# Проверки
# ---------------------------------------------------------------------------

check_compile() {
    info "Compiling: -std=c++17 -O2 -Wall -Wextra -Wpedantic"
    local ok_all=1
    build_one test_parser_release "-std=c++17 -O2 -Wall -Wextra -Wpedantic" tests/test_parser.cpp || ok_all=0
    build_one test_stream_release "-std=c++17 -O2 -Wall -Wextra -Wpedantic" tests/test_stream_reader.cpp || ok_all=0
    build_one test_formatter_release "-std=c++17 -O2 -Wall -Wextra -Wpedantic" tests/test_formatter.cpp || ok_all=0
    build_one fuzz_release "-std=c++17 -O2 -Wall -Wextra -Wpedantic" tests/fuzz_parser.cpp || ok_all=0
    if [ $ok_all -eq 1 ]; then ok "clean compilation (0 warnings, C++17)"; else bad "compilation warnings/errors"; fi
}

check_unit() {
    info "Unit tests (release)"
    local all_ok=1
    for t in test_parser_release test_stream_release test_formatter_release; do
        run_bin "$t" || { all_ok=0; bad "unit test $t failed"; }
    done
    [ $all_ok -eq 1 ] && ok "unit tests passed (test_parser, test_stream_reader, test_formatter)"
}

check_fuzz() {
    info "Fuzz test (release, $FUZZ_RANDOM random mutations)"
    run_bin fuzz_release "$FUZZ_RANDOM" || { bad "fuzz test failed"; return; }
    ok "fuzz test passed ($FUZZ_RANDOM random + deterministic mutations)"
}

check_sanitize() {
    info "Sanitizers: ASan + UBSan + leaks (-O1 -g)"
    local ok_all=1
    build_one test_parser_san "-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" tests/test_parser.cpp || ok_all=0
    build_one test_stream_san "-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" tests/test_stream_reader.cpp || ok_all=0
    build_one test_formatter_san "-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" tests/test_formatter.cpp || ok_all=0
    build_one fuzz_san "-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" tests/fuzz_parser.cpp || ok_all=0
    [ $ok_all -eq 0 ] && { bad "sanitizer build failed"; return; }

    for t in test_parser_san test_stream_san test_formatter_san; do
        run_bin "$t" || { bad "unit test $t under ASan/UBSan failed"; return; }
    done
    ok "unit tests clean under ASan/UBSan"

    ASAN_OPTIONS=detect_leaks=1 run_bin fuzz_san "$FUZZ_RANDOM_SAN" \
        || { bad "fuzz test under ASan/UBSan failed"; return; }
    ok "fuzz test ($FUZZ_RANDOM_SAN mutations) clean under ASan/UBSan - no overflows, UB, leaks"
}

check_noexceptions() {
    info "Without exceptions/RTTI: -fno-exceptions -fno-rtti"
    local ok_all=1
    build_one test_parser_ne "-std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra" tests/test_parser.cpp || ok_all=0
    build_one fuzz_ne "-std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra" tests/fuzz_parser.cpp || ok_all=0
    [ $ok_all -eq 0 ] && { bad "build without exceptions failed"; return; }
    run_bin test_parser_ne || { bad "unit test without exceptions failed"; return; }
    run_bin fuzz_ne "$FUZZ_RANDOM_SAN" || { bad "fuzz test without exceptions failed"; return; }
    ok "works without exceptions and RTTI (unit + fuzz)"
}

check_valgrind() {
    if ! command -v valgrind >/dev/null 2>&1; then
        info "valgrind not installed - skipping"
        return
    fi
    info "valgrind memcheck (fuzz, 50k mutations)"
    valgrind --error-exitcode=99 --leak-check=full \
        "$BUILD_DIR/fuzz_release" 50000 >"$BUILD_DIR/valgrind.log" 2>&1
    local rc=$?
    if [ $rc -ne 0 ]; then
        tail -25 "$BUILD_DIR/valgrind.log"
        bad "valgrind found memory errors"
        return
    fi
    ok "valgrind memcheck clean (no errors or leaks)"
}

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

case "${1:-all}" in
    unit)
        check_compile; check_unit
        ;;
    fuzz)
        FUZZ_RANDOM="${2:-$FUZZ_RANDOM}"
        check_compile; check_fuzz
        ;;
    sanitize)
        check_sanitize
        ;;
    noexceptions)
        check_noexceptions
        ;;
    valgrind)
        check_valgrind
        ;;
    clean)
        rm -rf "$BUILD_DIR"
        echo "[OK] Removed: $BUILD_DIR"
        exit 0
        ;;
    help|-h|--help)
        sed -n '2,27p' "$SELF" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    all)
        check_compile
        check_unit
        check_fuzz
        check_sanitize
        check_noexceptions
        check_valgrind
        ;;
    *)
        echo "Unknown command: $1 (see ./run_quality_checks.sh help)"
        exit 2
        ;;
esac

echo ""
echo -e "=== Summary: ${GREEN}${pass} OK${NC}, ${RED}${fail_total} FAIL${NC} ==="
[ $fail_total -eq 0 ] || exit 1
exit 0