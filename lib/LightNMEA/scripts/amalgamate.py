#!/usr/bin/env python3
"""
LightNMEA - single-header amalgamation script.

Собирает ядро библиотеки (types.hpp, light_nmea.hpp, nmea_formatter.hpp,
nmea_stream_reader.hpp, stream.hpp) в один заголовок lightnmea_single.hpp.

Использование:
    python3 scripts/amalgamate.py [output_path]

Платформозависимые заголовки (net_streams.hpp, serial_stream.hpp) в
амальгамацию не входят, так так они требуют POSIX.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # lib/LightNMEA
INCLUDE_DIR = ROOT / "include"
ENTRY = "lightnmea.h"
DEFAULT_OUTPUT = ROOT / "lightnmea_single.hpp"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*(//.*)?$', re.MULTILINE)
PRAGMA_ONCE_RE = re.compile(r'^\s*#\s*pragma\s+once\s*$', re.MULTILINE)

# Стандартный GPL-блок в начале каждого файла (один в шапке).
LICENSE_RE = re.compile(
    r'/\*\n \* LightNMEA - Ultra-fast NMEA GPS parser\n'
    r' \* Copyright \(C\) 2026 Roman Shevchik\n.*?\n \*/\n',
    re.DOTALL,
)


def strip_guard(text: str) -> str:
    """Убирает строку #pragma once и ifndef/define-гард, если есть."""
    text = PRAGMA_ONCE_RE.sub("", text)
    m = re.match(r'^\s*#\s*ifndef\s+(\w+)\s*\n\s*#\s*define\s+\1\s*\n', text)
    if m:
        text = text[m.end():]
        text = re.sub(r'\s*#\s*endif\s*(//.*)?\s*$', "", text)
    return text


def expand(path: Path, seen: set) -> str:
    text = path.read_text(encoding="utf-8")
    text = LICENSE_RE.sub("", text, count=1)
    text = strip_guard(text)

    def repl(match: re.Match) -> str:
        name = match.group(1)
        inc = INCLUDE_DIR / name
        if inc.exists():
            if inc in seen:
                return ""  # уже включено выше - убираем include-строку
            seen.add(inc)
            return expand(inc, seen)
        return match.group(0)  # системные/неизвестные include оставляем

    return INCLUDE_RE.sub(repl, text)


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT
    seen = {INCLUDE_DIR / ENTRY}
    body = expand(INCLUDE_DIR / ENTRY, seen)

    header = f"""/*
 * LightNMEA - Ultra-fast NMEA GPS parser
 * Copyright (C) 2026 Roman Shevchik
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file lightnmea_single.hpp
 * @brief single-header вариант LightNMEA.
 *
 * Сгенерировано скриптом scripts/amalgamate.py из:
 *   {", ".join(sorted(p.name for p in seen))}
 * Не редактировать вручную - править исходники и создать заново.
 */

#ifndef LIGHT_NMEA_SINGLE_HPP
#define LIGHT_NMEA_SINGLE_HPP

{body}
#endif /* LIGHT_NMEA_SINGLE_HPP */
"""
    out.write_text(header, encoding="utf-8")
    print(f"Amalgamated {len(seen)} headers -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
