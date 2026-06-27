#!/usr/bin/env python3
from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]


def main() -> None:
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("C++ host tests require g++ or clang++ on PATH")

    with tempfile.TemporaryDirectory(prefix="esp32-meteo-host-tests-") as tmpdir:
        binary = Path(tmpdir) / "test_firmware_logic"
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(PROJECT_DIR / "src"),
            str(PROJECT_DIR / "test" / "host" / "test_firmware_logic.cpp"),
            str(PROJECT_DIR / "src" / "firmware_logic.cpp"),
            str(PROJECT_DIR / "src" / "battery_curve.cpp"),
            str(PROJECT_DIR / "src" / "local_button_logic.cpp"),
            str(PROJECT_DIR / "src" / "runtime_config.cpp"),
            str(PROJECT_DIR / "src" / "provisioning_logic.cpp"),
            "-o",
            str(binary),
        ]
        print(" ".join(command))
        subprocess.run(command, cwd=PROJECT_DIR, check=True)
        subprocess.run([str(binary)], cwd=PROJECT_DIR, check=True)
        print("Host firmware logic tests passed")


if __name__ == "__main__":
    main()
