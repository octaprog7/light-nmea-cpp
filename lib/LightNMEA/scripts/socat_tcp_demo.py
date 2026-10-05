#!/usr/bin/env python3
"""Детерминированная демонстрация: socat (pty <-> TCP) + tcp_stream_example.

Проверяет полную цепочку:
    socat (pty -> TCP-сервер) -> tcp_stream_example (TCP-клиент) -> Parser

ВАЖНО про режим socat (не допустить флакинга):
  * НЕ используем флаг `fork`. Он порождает новый дочерний процесс socat на
    каждое входящее TCP-подключение, и каждый такой процесс выделяет свой
    /dev/pts/X. Скрипт знает только первый /dev/pts/X (из первой строки лога),
    поэтому при переподключении клиента данные ушли бы в неизвестный порт.
  * Вместо этого держим ОДИНОЧНОЕ соединение: `TCP-LISTEN:PORT,reuseaddr`
    (без fork) принимает один клиент и остаётся с ним до конца.

Запуск:
    python3 socat_tcp_demo.py [port]

Скрипт сам соберёт цель tcp_stream_example, если она ещё не собрана.
"""

import os
import re
import signal
import subprocess
import sys
import time

CMAKE_DIR = os.path.join(os.path.dirname(__file__), "..")
BUILD_DIR = os.path.join(CMAKE_DIR, "build", "demo")


def build_target():
    """Собирает tcp_stream_example (header-only библиотека, сборка быстрая)."""
    os.makedirs(BUILD_DIR, exist_ok=True)
    subprocess.run(["cmake", "-S", CMAKE_DIR, "-B", BUILD_DIR,
                    "-DCMAKE_BUILD_TYPE=Release"],
                   check=True, capture_output=True)
    subprocess.run(["cmake", "--build", BUILD_DIR, "-j",
                    "--target", "tcp_stream_example"],
                   check=True, capture_output=True)
    return os.path.join(BUILD_DIR, "tcp_stream_example")


def find_pty(socat, timeout_s=5.0):
    """Читает stderr socat и извлекает путь к созданному псевдотерминалу."""
    deadline = time.time() + timeout_s
    for line in socat.stderr:
        m = re.search(r"PTY is (/dev/pts/\d+)", line)
        if m:
            return m.group(1)
        if time.time() > deadline:
            break
    return None


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "12947"
    exe = build_target()

    # ОДИНОЧНОЕ соединение: БЕЗ fork (см. комментарий в шапке файла).
    socat = subprocess.Popen(
        ["socat", "-d", "-d", "-D", "PTY,raw,echo=0",
         f"TCP-LISTEN:{port},reuseaddr"],
        stderr=subprocess.PIPE, text=True)

    try:
        pty_dev = find_pty(socat)
        if not pty_dev:
            print("FAIL: socat did not create a pty", file=sys.stderr)
            return 1
        print("socat pty:", pty_dev)

        # Небольшая пауза, чтобы TCP-слушатель был готов.
        time.sleep(0.5)

        client = subprocess.Popen([exe, "127.0.0.1", port],
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        time.sleep(1.0)

        nmea = ("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,"
                "230394,003.1,W*6A\r\n"
                "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,"
                "47.0,M,,*4F\r\n")
        with open(pty_dev, "wb") as f:
            f.write(nmea.encode())

        time.sleep(1.5)
        os.kill(client.pid, signal.SIGINT)
        out, err = client.communicate(timeout=5)

        print("=== CLIENT STDOUT ===")
        print(out)
        if err:
            print("=== CLIENT STDERR ===")
            print(err)

        # Проверка: два валидных пакета GPS (cst=1).
        # Регэксп устойчив к регистру и лишним пробелам вокруг '=', чтобы
        # проверка не ломалась при изменении форматирования вывода C++.
        marker = re.compile(
            r"recognized\s*=\s*1\s+valid\s*=\s*1\s+cst\s*=\s*1",
            re.IGNORECASE)
        ok = len(marker.findall(out)) >= 2
        print("PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        socat.kill()


if __name__ == "__main__":
    sys.exit(main())