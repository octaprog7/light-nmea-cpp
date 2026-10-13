#!/usr/bin/env python3
"""code_report.py - report on the compiled release code of LightNMEA.

Python-реализация анализа, ранее бывшего в code_report.sh.
Весь анализ построен на вызове СТАНДАРТНЫХ утилит GNU binutils
(size / nm / objdump / readelf / file) через subprocess.

Отчёт показывает:
  1. Code size: .text / .data / .bss / .rodata (size), Flash = text+data(+rodata),
     RAM = data+bss.
  2. FPU: absence of hardware FPU instructions in the disassembly (objdump -d)
     and absence of soft-float calls (nm -u: __aeabi_f*/__aeabi_d*,
     __divsf3/__divdf3 and similar).
  3. Dependencies: NEEDED entries (readelf -d) and external symbols (nm -u),
     classified into "runtime" (memcpy and similar) and real dependencies.
  4. Memory: absence of malloc/calloc/realloc/free/operator new|delete/
     sbrk/brk/mmap (nm).
  5. Code density: number of instructions, average instruction width (bytes),
     instructions per 1 KB of code (objdump -d + size).

Анализируется объектный файл сценария scripts/code_report_probe.cpp
(целочисленное ядро: parse_line + raw values; float-геттеры и форматтеры
не включаются). Можно передать готовый .o/.elf/.a.

Usage:
  ./code_report.py                # auto: Cortex-M0+ (если есть arm-none-eabi-g++)
                                  # otherwise native (x86-64)
  ./code_report.py --native       # build the probe with the native compiler
  ./code_report.py --m0           # build the probe for Cortex-M0+ (soft-float)
  ./code_report.py --quick (-q)   # brief output: facts only (FPU/deps/memory/size)
  ./code_report.py <file>         # analyze a ready .o/.elf/.a
  ./code_report.py clean          # remove build-code-report/
  ./code_report.py help

Exit code: 0 - all checks passed; 1 - violations found.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

# ---------------------------------------------------------------------------
# Цвета вывода (как в прежней bash-версии)
# ---------------------------------------------------------------------------
RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[0;36m"
NC = "\033[0m"

HELP = __doc__


class Report:
    """Счётчики PASS/FAIL и форматированный вывод секций отчёта."""

    def __init__(self, tools: Dict[str, str], arch: str) -> None:
        self.tools = tools
        self.arch = arch
        self.pass_n = 0
        self.fail_n = 0

    def ok(self, msg: str) -> None:
        print(f"  {GREEN}[PASS]{NC} {msg}")
        self.pass_n += 1

    def bad(self, msg: str) -> None:
        print(f"  {RED}[FAIL]{NC} {msg}")
        self.fail_n += 1

    def info(self, msg: str) -> None:
        print(f"  {YELLOW}[..]{NC} {msg}")

    def hdr(self, title: str) -> None:
        print(f"{CYAN}--- {title} ---{NC}")


# ---------------------------------------------------------------------------
# Регулярные выражения FPU по архитектурам (перенесены из bash-версии)
# ---------------------------------------------------------------------------

# ARM (VFP/NEON): vldr/vstr/vpush/vpop, арифметика с суффиксом .f32/.f64,
# vcvt*, vmrs/vmsr. Целочисленные NEON-операции (.i8/.i16/.../qadd и т.п.)
# намеренно НЕ считаются FPU.
FPU_RE_ARM = (
    r"\bv(ldr|str|push|pop)\b"
    r"|\bv(add|sub|mul|div|mla|mls|nmul|nmla|nmls|abs|neg|sqrt|cmp|cmpz|"
    r"mov|max|min|fma|fms|fnma|fnms)\.f(16|32|64)\b"
    r"|\bvcvt\w*\b|\bvm(rs|sr)\b"
)

# AArch64: fadd/fmul/fcvt/... + загрузки d0/s0 регистров.
FPU_RE_AARCH64 = (
    r"\bf(add|sub|mul|div|madd|msub|nmadd|nmsub|abs|neg|sqrt|cmp|ccmp|ccmpe|"
    r"mov|sel|max|min|rint[a-z]*|cvt[a-z]*)\b"
    r"|\b(scvtf|ucvtf|fcvtzs|fcvtzu|fcvtn|fcvtns|fcvtnu|fcvtl|fcvtls)\b"
    r"|\b(ldr|str)\s+[ds][0-9]+\b"
)

# x86/x86-64: SSE/SSE2/AVX (sd/ss/ps/pd), конверсии, сравнения и полный x87.
FPU_RE_X86 = (
    r"\bv?(add|sub|mul|div|sqrt|max|min|rcp|rsqrt)(ss|sd|ps|pd)\b"
    r"|\bcvt[a-z0-9]*(ss|sd|ps|pd|si|dq|pi|ts|td|us|ui|sx|tx)\b"
    r"|\bu?comi(sd|ss|ps|pd)\b"
    r"|\bf(ld|stp?|ild|istp?|add|addp|sub|subp|mul|mulp|div|divp|abs|chs|"
    r"sqrt|com|ucom|comip|ucomip|rndint|xch|scale|prem|patan|ptan|sin|cos|"
    r"yl2x|2xm1|init|ninit|wait|nclex|clex|free|decstp|incstp|ldcw|stcw|"
    r"ldenv|stenv|rstor|save|nstsw|stsw)\w*\b"
)

# RISC-V: F/D расширения (flw/fld/fsw/fsd, fadd.s/fdiv.d/fcvt.*/...).
FPU_RE_RISCV = (
    r"\b(fadd|fsub|fmul|fdiv|fsqrt|fmin|fmax|fmadd|fmsub|fnmadd|fnmsub|"
    r"fcvt|fmv|feq|flt|fle|fclass)\.[sdq]x?\w*\b"
    r"|\bfl[wdq]\b|\bfs[wdq]\b"
)

FPU_RE_GENERIC = (
    r"\bv[a-z]+\.f(16|32|64)\b"
    r"|\b(fadd|fsub|fmul|fdiv|fsqrt|vcvt|fcvt|flw|fsw|fld|fsd|movsd|movss|"
    r"addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si)\b"
)

FPU_RES: Dict[str, "re.Pattern[str]"] = {
    "arm": re.compile(FPU_RE_ARM),
    "aarch64": re.compile(FPU_RE_AARCH64),
    "x86": re.compile(FPU_RE_X86),
    "riscv": re.compile(FPU_RE_RISCV),
    "generic": re.compile(FPU_RE_GENERIC),
}

# ---------------------------------------------------------------------------
# Допустимый runtime: символы, которые НЕ считаются внешней зависимостью
# ---------------------------------------------------------------------------

# _ZSt* - libstdc++ runtime (std::terminate, __throw_* и т.п.)
# __pre_init_* - pico-sdk preinit-функции (ссылки из .preinit_array прошивки)
RT_OK_RE = re.compile(
    r"^(memcpy|memmove|memset|memcmp|strlen|strcmp|strncpy|strncmp|abort|"
    r"__stack_chk_fail|__assert_func|__assert_fail|__cxa_[a-zA-Z0-9_]+|"
    r"_Unwind_[A-Za-z0-9_]+|__gxx_personality_v0|__aeabi_unwind_cpp_pr0|"
    r"__aeabi_unwind_cpp_pr1|__aeabi_read_tp|__aeabi_errno_addr|"
    r"_ZSt[a-zA-Z0-9_]*|__pre_init_[A-Za-z0-9_]*)$"
)

# Целочисленные runtime-помощники (деление без аппаратной поддержки и т.п.)
INT_HELPERS_RE = re.compile(
    r"^(__aeabi_(memcpy|memmove|memset|memclr|uidiv|idiv|uidivmod|idivmod|"
    r"uldivmod|ldivmod|llsr|llsl|lasr|lmul|l2)|__divsi3|__udivsi3|"
    r"__modsi3|__umodsi3|__divdi3|__udivdi3|__moddi3|__umoddi3|"
    r"__muldi3|__mulsi3|__udivmoddi4)$"
)

# Float-помощники soft-float runtime: их наличие = float-арифметика в коде
# (реализованная программно, без аппаратного FPU).
FLOAT_HELPERS_RE = re.compile(
    r"(__aeabi_[fd][a-z0-9_]*|__divsf3|__divdf3|__mulsf3|__muldf3|__addsf3|"
    r"__adddf3|__subsf3|__subdf3|__negsf2|__negdf2|__cmpsf2|__cmpdf2|"
    r"__fixsfdi|__fixdfdi|__fixsfsi|__fixdfsi|__fixunssfsi|__fixunsdfsi|"
    r"__fixunssfdi|__fixunsdfdi|__floatsisf|__floatdidf|__floatsidf|"
    r"__floatunsisf|__floatunsidf|__extendsfdf2|__truncdfsf2|"
    r"__powisf2|__powidf2)"
)

# Строка инструкции в дизассемблере: "  addr:" или "addr:",
# мнемоника начинается с буквы a-z (метки "<имя>:" и данные .word/.byte не
# попадают в выборку).
INSN_LINE_RE = re.compile(r"^\s*[0-9a-fA-F]+:\s+[a-z]")

# Директивы данных, исключаемые при подсчёте байт кода.
DATA_DIRECTIVES_RE = re.compile(
    r"\.(word|byte|short|long|inst|pool|align|ascii|fill|zero)"
)

# Выделение и операторы new/delete для проверки памяти.
HEAP_RE = re.compile(
    r"(malloc|calloc|realloc|free|aligned_alloc|posix_memalign|memalign|"
    r"valloc|pvalloc|sbrk|brk|mmap|munmap)|_Z(nw|na|dl|da)[A-Za-z0-9_]*"
)

# Тип символа в выводе nm - это одиночная буква.
SYM_TYPE_RE = re.compile(r"^[A-Za-z]$")


# ---------------------------------------------------------------------------
# Вспомогательные функции
# ---------------------------------------------------------------------------

def run_tool(argv: Sequence[str]) -> Optional[subprocess.CompletedProcess]:
    """Запускает внешний инструмент, захватывая stdout/stderr.

    Возвращает None, если инструмент не найден или запуск завершился
    исключением; проверку exit-кода делает вызывающий код.
    """
    try:
        return subprocess.run(list(argv), capture_output=True, text=True)
    except (OSError, subprocess.SubprocessError):
        return None


def have(tool: str) -> bool:
    return shutil.which(tool) is not None


def fmt_bytes(b: int) -> str:
    if b < 1024:
        return f"{b} B"
    if b < 1048576:
        return f"{b / 1024:.2f} KiB"
    return f"{b / 1048576:.2f} MiB"


def detect_arch(path: str) -> str:
    """Определяет архитектуру файла: file -b, затем readelf -h."""
    desc = ""
    if have("file"):
        proc = run_tool(["file", "-b", path])
        if proc is not None:
            desc = proc.stdout

    for marker, arch in (("AArch64", "aarch64"), ("ARM", "arm"),
                         ("x86-64", "x86"), ("Intel 80386", "x86"),
                         ("i386", "x86"), ("RISC-V", "riscv")):
        if marker in desc:
            return arch

    proc = run_tool(["readelf", "-h", path])
    if proc is not None:
        for line in proc.stdout.splitlines():
            m = re.match(r"\s*Machine:\s*(.*)", line)
            if not m:
                continue
            machine = m.group(1)
            for marker, arch in (("AArch64", "aarch64"), ("ARM", "arm"),
                                 ("X86-64", "x86"), ("Intel 80386", "x86"),
                                 ("RISC-V", "riscv")):
                if marker in machine:
                    return arch
            break
    return "generic"


def pick_tools(arch: str) -> Dict[str, str]:
    """Выбирает тулчейн: для ARM/AArch64 - arm-none-eabi-*, если доступен."""
    tools = {"objdump": "objdump", "nm": "nm", "size": "size",
             "readelf": "readelf", "file": "file"}
    if arch in ("arm", "aarch64") and have("arm-none-eabi-objdump"):
        for tool in ("objdump", "nm", "size", "readelf"):
            tools[tool] = f"arm-none-eabi-{tool}"
    return tools


# ---------------------------------------------------------------------------
# Получение данных от binutils
# ---------------------------------------------------------------------------

def disasm_lines(tools: Dict[str, str], f: str, *, raw: bool) -> List[str]:
    """Строки objdump -d; raw=True - с байтами кода (для подсчёта ширины)."""
    argv = [tools["objdump"], "-d"]
    if not raw:
        argv.append("--no-show-raw-insn")
    argv.append(f)
    proc = run_tool(argv)
    return proc.stdout.splitlines() if proc is not None else []


def count_insns(tools: Dict[str, str], f: str) -> int:
    """Число реальных инструкций (строк 'адрес: мнемоника')."""
    lines = disasm_lines(tools, f, raw=False)
    return sum(1 for ln in lines if INSN_LINE_RE.match(ln))


def instruction_bytes(lines: Iterable[str]) -> int:
    """Сумма байт-кода: hex-токены между 'адрес:' и мнемоникой.

    Эквивалент awk-скрипта из bash-версии: собираем hex-токены чётной длины
    до первого не-hex токена; строки данных (.word/.byte/...) исключаются.
    """
    total = 0
    for line in lines:
        m = re.match(r"\s*[0-9a-fA-F]+:\s*(.*)$", line)
        if m is None or DATA_DIRECTIVES_RE.search(line):
            continue
        for tok in re.split(r"[, \t]+", m.group(1)):
            if re.fullmatch(r"[0-9a-fA-F]+", tok) and len(tok) % 2 == 0:
                total += len(tok) // 2
            else:
                break
    return total


def collect_undef_names(tools: Dict[str, str], f: str) -> List[str]:
    """Отсортированный уникальный список внешних символов (nm -u)."""
    proc = run_tool([tools["nm"], "-u", f])
    if proc is None:
        return []
    names = set()
    for line in proc.stdout.splitlines():
        parts = line.split()
        if not parts:
            continue
        # Имя - последний токен строки (надёжно для форматов "U foo"
        # и "0000 U foo@GLIBC_x"); версия после '@' отбрасывается.
        name = parts[-1].split("@", 1)[0]
        if name:
            names.add(name)
    return sorted(names)


def classify_symbols(names: Sequence[str]) -> Tuple[List[str], List[str], List[str], List[str]]:
    """Раскладывает внешние символы на runtime/int-runtime/float/прочие."""
    runtime = [n for n in names if RT_OK_RE.fullmatch(n)]
    ints = [n for n in names if INT_HELPERS_RE.fullmatch(n)]
    floats = [n for n in names if FLOAT_HELPERS_RE.search(n)]
    other = [n for n in names
             if not (RT_OK_RE.fullmatch(n) or INT_HELPERS_RE.fullmatch(n)
                     or FLOAT_HELPERS_RE.search(n))]
    return runtime, ints, floats, other


def needed_libs(tools: Dict[str, str], f: str) -> List[str]:
    """Записи NEEDED из readelf -d."""
    proc = run_tool([tools["readelf"], "-d", f])
    if proc is None:
        return []
    return [ln.strip() for ln in proc.stdout.splitlines() if "NEEDED" in ln]


def soft_float_syms(tools: Dict[str, str], f: str) -> List[str]:
    """Внешние символы soft-float runtime (nm -u, фильтр FLOAT_HELPERS)."""
    names = collect_undef_names(tools, f)
    return sorted(n for n in names if FLOAT_HELPERS_RE.search(n))


def heap_syms(tools: Dict[str, str], f: str) -> Tuple[List[str], List[str], List[str]]:
    """undef/defined аллокаторы и alloca по выводу nm.

    Поддерживаются оба формата nm: 'U malloc' (объектный файл, адрес
    отсутствует) и '0000 U malloc' (с адресом). Прежняя bash-версия
    распознавала только формат с адресом (проверка $2 ~ /^[Uu]$/).
    """
    proc = run_tool([tools["nm"], f])
    if proc is None:
        return [], [], []
    undef_heap: List[str] = []
    def_heap: List[str] = []
    alloca: List[str] = []
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        if SYM_TYPE_RE.fullmatch(parts[0]):
            typ, name = parts[0], parts[1]
        elif len(parts) >= 3 and SYM_TYPE_RE.fullmatch(parts[1]):
            typ, name = parts[1], parts[-1]
        else:
            continue
        name = name.split("@", 1)[0]
        if HEAP_RE.search(name):
            if typ in ("U", "u"):
                undef_heap.append(name)
            elif typ in ("T", "t", "W", "w", "D", "d", "B", "b", "R", "r"):
                def_heap.append(name)
        if re.fullmatch(r"(?:__alloca|_alloca|alloca)", name):
            alloca.append(line.strip())
    return sorted(set(undef_heap)), sorted(set(def_heap)), alloca


def size_text_data_bss(tools: Dict[str, str], f: str) -> Optional[Tuple[int, int, int]]:
    """(text, data, bss) из второй строки вывода `size`."""
    proc = run_tool([tools["size"], f])
    if proc is None:
        return None
    lines = proc.stdout.splitlines()
    if len(lines) < 2:
        return None
    parts = lines[1].split()
    if len(parts) < 3:
        return None
    try:
        return int(parts[0]), int(parts[1]), int(parts[2])
    except ValueError:
        return None


def rodata_bytes(tools: Dict[str, str], f: str) -> int:
    """Сумма размеров секций .rodata* (size -A)."""
    proc = run_tool([tools["size"], "-A", f])
    if proc is None:
        return 0
    total = 0
    for line in proc.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and ".rodata" in fields[0]:
            try:
                total += int(fields[1])
            except ValueError:
                pass
    return total


def readelf_header(tools: Dict[str, str], f: str) -> List[Tuple[str, str]]:
    """Поля Class/Data/Entry point из readelf -h."""
    proc = run_tool([tools["readelf"], "-h", f])
    if proc is None:
        return []
    out: List[Tuple[str, str]] = []
    for line in proc.stdout.splitlines():
        m = re.match(r"^\s*(Class|Data|Entry point[^:]*):\s*(.*)$", line)
        if m:
            out.append((m.group(1).strip(), m.group(2).strip()))
    return out


def eabi_attrs(tools: Dict[str, str], f: str) -> List[str]:
    """FPU-атрибуты EABI из readelf -A (только для ARM)."""
    proc = run_tool([tools["readelf"], "-A", f])
    if proc is None:
        return []
    return [ln for ln in proc.stdout.splitlines()
            if re.search(r"Tag_FP_arch|Tag_ABI_VFP_args|Tag_ABI_FP_16bit_format",
                         ln, re.IGNORECASE)]


# ---------------------------------------------------------------------------
# Секции отчёта
# ---------------------------------------------------------------------------

def report_header(r: Report, f: str, arch: str) -> None:
    r.hdr("File and target")
    desc = "?"
    if have(r.tools["file"]):
        proc = run_tool([r.tools["file"], "-b", f])
        if proc is not None and proc.stdout.strip():
            desc = proc.stdout.strip()
    print(f"  File:    {f}")
    print(f"  Architecture: {arch} ({desc})")
    for name, value in readelf_header(r.tools, f):
        print(f"  {name}: {value}")


def report_size(r: Report, f: str) -> None:
    r.hdr("1. Code size (size)")
    tdb = size_text_data_bss(r.tools, f)
    if tdb is None:
        r.info("failed to get sizes (size)")
        return
    text, data, bss = tdb
    rodata = rodata_bytes(r.tools, f)
    flash = text + data + rodata
    ram = data + bss

    print(f"  .text (code):   {text} ({fmt_bytes(text)})")
    print(f"  .rodata:        {fmt_bytes(rodata)}")
    print(f"  .data:          {fmt_bytes(data)}")
    print(f"  .bss:           {fmt_bytes(bss)}")
    print(f"  Flash ~ text+data+rodata: {fmt_bytes(flash)}")
    print(f"  RAM  ~ data+bss:          {fmt_bytes(ram)}")
    r.ok(f"code size: {text} bytes ({fmt_bytes(text)})")


def report_fpu(r: Report, f: str, arch: str) -> None:
    r.hdr("2. FPU (objdump -d + nm -u)")
    fpu_re = FPU_RES.get(arch, FPU_RES["generic"])

    # Считаем только строки реальных инструкций (не .word/.byte-данные,
    # не адреса меток).
    lines = [ln for ln in disasm_lines(r.tools, f, raw=False)
             if INSN_LINE_RE.match(ln)]
    hits = [ln for ln in lines if fpu_re.search(ln)]
    total = len(hits)
    examples = hits[:6]

    # Soft-float вызовы (float-математика без аппаратного FPU)
    sf = soft_float_syms(r.tools, f)

    if total == 0 and not sf:
        r.ok("no hardware FPU instructions: 0")
    else:
        r.bad(f"FPU instructions found: {total}")
        for ln in examples:
            print(f"       {ln}")
    if sf:
        r.bad(f"soft-float calls: {' '.join(sf)}")
    else:
        r.ok("no soft-float calls (__aeabi_f*/__aeabi_d*, __divsf3 and similar): 0")

    # Для ARM - атрибуты EABI из объектного файла (readelf -A)
    if arch == "arm":
        for ln in eabi_attrs(r.tools, f):
            print(f"  (readelf -A) {ln}")


def report_deps(r: Report, f: str) -> None:
    r.hdr("3. Dependencies (readelf -d + nm -u)")

    # Динамические библиотеки
    needed = needed_libs(r.tools, f)
    if needed:
        r.bad("dynamic dependencies (NEEDED):")
        for ln in needed:
            print(f"       {ln}")
    else:
        r.ok("no dynamic dependencies (NEEDED)")

    # Внешние символы
    names = collect_undef_names(r.tools, f)
    runtime, ints, floats, other = classify_symbols(names)

    if not other:
        what = ""
        if runtime:
            what += f", runtime: {' '.join(runtime)}"
        if ints:
            what += f", int-runtime: {' '.join(ints)}"
        if not what:
            what = " (no external symbols)"
        r.ok(f"external symbols - only allowed runtime:{what}")
    else:
        r.bad("external dependencies found:")
        for n in other:
            print(f"       {n}")
    if floats:
        r.info(f"float-runtime symbols (see the FPU section): {' '.join(floats)}")


def report_heap(r: Report, f: str) -> None:
    r.hdr("4. Heap allocation (nm)")

    # U/u = внешняя ссылка (код ВЫЗЫВАЕТ аллокатор) -> нарушение.
    # T/W/D/B/R = символ ОПРЕДЕЛЁН в образе (образ ПРЕДОСТАВЛЯЕТ аллокатор,
    #   например newlib в прошивке) -> это не использование библиотекой.
    undef_heap, def_heap, alloca = heap_syms(r.tools, f)

    if not undef_heap:
        r.ok("code does not call allocators: malloc/calloc/realloc/free/new/delete/sbrk/mmap = 0")
    else:
        r.bad("code calls allocators:")
        for n in undef_heap:
            print(f"       {n}")
    if def_heap:
        r.info("allocator defined in the image (provided, not called):")
        for n in def_heap:
            print(f"       {n}")
    if alloca:
        r.info(f"alloca (stack, not heap): {' '.join(alloca)}")
    else:
        r.ok("alloca is not used")


def report_density(r: Report, f: str, arch: str) -> None:
    r.hdr("5. Code density (objdump -d + size)")

    # Инструкция = "адрес: мнемоника" (мнемоника начинается с буквы a-z;
    # метки имеют формат "<имя>:", данные - ".word"/".byte"/... и не попадают).
    n = count_insns(r.tools, f)

    # Сумма байтов кода: в raw-выводе после "addr:" идут hex-токены кода,
    # затем мнемоника.
    b = instruction_bytes(disasm_lines(r.tools, f, raw=True))

    avg = f"{b / n:.2f}" if n > 0 else "n/a"
    per_kb = f"{n / (b / 1024):.0f}" if b > 0 else "n/a"

    print(f"  Instructions:          {n}")
    print(f"  Code bytes (instr.):   {b}")
    print(f"  Avg instruction width: {avg} bytes/instr")
    print(f"  Density:               {per_kb} instr./KB")

    hints = {
        "arm": "Thumb/Thumb-2: theoretical minimum 2 bytes/instr",
        "x86": "x86: variable length 1-15 bytes/instr",
        "aarch64": "AArch64: fixed 4 bytes/instr",
        "riscv": "RISC-V: 2 or 4 bytes/instr",
    }
    hint = hints.get(arch)
    if hint:
        r.info(hint)
    r.ok(f"code density: {per_kb} instr./KB (avg width {avg} bytes)")


# ---------------------------------------------------------------------------
# Краткий отчёт (--quick): только факты, одной таблицей
# ---------------------------------------------------------------------------

def report_brief(r: Report, f: str, arch: str) -> int:
    fpu_re = FPU_RES.get(arch, FPU_RES["generic"])

    # --- размер (size) ---
    tdb = size_text_data_bss(r.tools, f)
    text, data, bss = tdb if tdb is not None else (0, 0, 0)
    rodata = rodata_bytes(r.tools, f)
    flash = text + data + rodata
    ram = data + bss

    # --- FPU (objdump -d + nm -u) ---
    lines = [ln for ln in disasm_lines(r.tools, f, raw=False)
             if INSN_LINE_RE.match(ln)]
    fpu_count = sum(1 for ln in lines if fpu_re.search(ln))
    sf_count = len(soft_float_syms(r.tools, f))

    # --- зависимости (readelf -d + nm -u) ---
    needed = needed_libs(r.tools, f)
    names = collect_undef_names(r.tools, f)
    _runtime, _ints, _floats, other = classify_symbols(names)

    # --- память (nm) ---
    undef_heap, _def_heap, _alloca = heap_syms(r.tools, f)

    # --- вердикты ---
    st_fpu = "FAIL" if (fpu_count > 0 or sf_count > 0) else "OK"
    st_dep = "FAIL" if (other or needed) else "OK"
    st_mem = "FAIL" if undef_heap else "OK"

    c_ok = f"{GREEN}{st_fpu}{NC}" if st_fpu == "OK" else f"{RED}{st_fpu}{NC}"
    c_dep = f"{GREEN}{st_dep}{NC}" if st_dep == "OK" else f"{RED}{st_dep}{NC}"
    c_mem = f"{GREEN}{st_mem}{NC}" if st_mem == "OK" else f"{RED}{st_mem}{NC}"

    deps_txt = "no external dependencies"
    if other:
        deps_txt = "HAVE external: " + " ".join(other)
    if needed:
        deps_txt += f"; NEEDED: {len(needed)}"

    print(f"=== LightNMEA: code ({arch}) ===")
    print(f"  Code size    : {text} B (Flash ~{fmt_bytes(flash)}, RAM {fmt_bytes(ram)})")
    print(f"  FPU          : none - instructions {fpu_count}, "
          f"soft-float {sf_count}  [{c_ok}]")
    print(f"  Dependencies : {deps_txt}  [{c_dep}]")
    print(f"  Memory       : allocator calls {len(undef_heap)}  [{c_mem}]")

    # --- плотность ---
    n = count_insns(r.tools, f)
    b = instruction_bytes(disasm_lines(r.tools, f, raw=True))
    avg = f"{b / n:.2f}" if n > 0 else "n/a"
    per_kb = f"{n / (b / 1024):.0f}" if b > 0 else "n/a"
    print(f"  Density      : {n} instr., {avg} bytes/instr, {per_kb} instr./KB")

    print()
    fails = (0 if st_fpu == "OK" else 1) \
        + (0 if st_dep == "OK" else 1) \
        + (0 if st_mem == "OK" else 1)
    if fails == 0:
        print(f"{GREEN}[PASS] no FPU usage, no external dependencies, "
              f"no heap allocation{NC}")
        return 0
    print(f"{RED}[FAIL] violations found: {fails}{NC}")
    return 1


# ---------------------------------------------------------------------------
# Сборка probe
# ---------------------------------------------------------------------------

def build_probe(target: str, build_dir: str, probe_src: str, inc: str) -> str:
    """Компилирует code_report_probe.cpp; весь вывод - в stderr,
    в stdout возвращается только путь к объектному файлу."""
    if target == "native":
        cxx = os.environ.get("CXX", "g++")
        flags = "-std=c++17 -O3 -march=native -fno-exceptions -fno-rtti"
        desc = f"native ({cxx})"
    elif target == "m0":
        cxx = os.environ.get("CROSS_CXX", "arm-none-eabi-g++")
        flags = ("-std=c++17 -O3 -mcpu=cortex-m0plus -mthumb "
                 "-mfloat-abi=soft -fno-exceptions -fno-rtti -ffreestanding")
        desc = "Cortex-M0+ (thumb, soft-float, no FPU)"
    else:
        print(f"Unknown target: {target}", file=sys.stderr)
        sys.exit(2)

    out = os.path.join(build_dir, f"probe_{target}.o")
    log = os.path.join(build_dir, f"probe_{target}.log")

    print(f"{CYAN}=== Building probe (Release) ==={NC}", file=sys.stderr)
    print(f"  Compiler:   {cxx}", file=sys.stderr)
    print(f"  Flags:      {flags}", file=sys.stderr)

    argv = ([cxx, "-c", probe_src, "-o", out]
            + flags.split()
            + [f"-I{inc}", "-Wall", "-Wextra"])
    try:
        with open(log, "w") as log_file:
            proc = subprocess.run(argv, stdout=log_file, stderr=log_file)
        rc = proc.returncode
    except OSError as exc:
        print(f"{RED}[FAIL] probe build failed: {exc}{NC}", file=sys.stderr)
        sys.exit(1)

    if rc != 0:
        print(f"{RED}[FAIL] probe build failed:{NC}", file=sys.stderr)
        with open(log) as log_file:
            sys.stderr.write(log_file.read())
        sys.exit(1)
    if os.path.getsize(log) > 0:
        print(f"  {YELLOW}[..]{NC} compiler warnings:", file=sys.stderr)
        with open(log) as log_file:
            sys.stderr.write(log_file.read())
    print(f"  Object:     {out}", file=sys.stderr)
    print("", file=sys.stderr)
    return out


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: Sequence[str]) -> int:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    lib_dir = os.path.dirname(script_dir)          # lib/LightNMEA
    inc = os.path.join(lib_dir, "include")
    probe_src = os.path.join(script_dir, "code_report_probe.cpp")
    build_dir = os.path.join(lib_dir, "build-code-report")
    os.makedirs(build_dir, exist_ok=True)

    target = "auto"
    file_arg = ""
    brief = False

    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg in ("help", "-h", "--help", "-help"):
            print(HELP, end="")
            return 0
        if arg == "clean":
            shutil.rmtree(build_dir, ignore_errors=True)
            print(f"Removed: {build_dir}")
            return 0
        if arg in ("--quick", "-q"):
            brief = True
        elif arg == "--native":
            target = "native"
        elif arg == "--m0":
            target = "m0"
        elif arg.startswith("--target="):
            target = arg.split("=", 1)[1]
        elif arg.startswith("-"):
            print(f"Unknown option: {arg} (see ./code_report.py help)",
                  file=sys.stderr)
            return 2
        else:
            file_arg = arg
        i += 1

    # ---- выбор файла для анализа ----
    if file_arg:
        # путь дан относительно каталога запуска
        if not os.path.isfile(file_arg):
            print(f"{RED}Error: file not found: {file_arg}{NC}", file=sys.stderr)
            return 1
        analyze_file = os.path.abspath(file_arg)
    else:
        if target == "auto":
            target = "m0" if have("arm-none-eabi-g++") else "native"
        if target not in ("native", "m0"):
            print(f"Unknown target: {target} (native|m0|auto)", file=sys.stderr)
            return 2
        if target == "m0" and not have("arm-none-eabi-g++"):
            print(f"{YELLOW}arm-none-eabi-g++ not found - switching to native{NC}",
                  file=sys.stderr)
            target = "native"
        analyze_file = build_probe(target, build_dir, probe_src, inc)

    if not os.path.isfile(analyze_file):
        print(f"{RED}Error: file does not exist: {analyze_file}{NC}",
              file=sys.stderr)
        return 1

    # ---- анализ ----
    arch = detect_arch(analyze_file)
    tools = pick_tools(arch)
    r = Report(tools, arch)

    if brief:
        return report_brief(r, analyze_file, arch)

    print()
    print(f"{CYAN}================ LightNMEA: code report ================{NC}")

    report_header(r, analyze_file, arch)

    base = os.path.basename(analyze_file)
    if base.startswith("probe_") and base.endswith(".o"):
        r.info("analyzing the library core (integer core: parse_line + raw values)")
        r.info("NMEAData float getters (*_deg, speed_*, altitude_m) and nmea_formatter")
        r.info("are not included - they pull in float arithmetic on demand")
    else:
        r.info("analyzing a ready file: sizes/dependencies cover the whole image")

    report_size(r, analyze_file)
    report_fpu(r, analyze_file, arch)
    report_deps(r, analyze_file)
    report_heap(r, analyze_file)
    report_density(r, analyze_file, arch)

    print()
    print(f"=== Summary: {GREEN}{r.pass_n} PASS{NC}, {RED}{r.fail_n} FAIL{NC} ===")
    print(f"  Tools: {tools['size']}, {tools['nm']}, "
          f"{tools['objdump']}, {tools['readelf']}")
    print(f"  Disassembler: {tools['objdump']} -d {analyze_file}")

    return 1 if r.fail_n > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))