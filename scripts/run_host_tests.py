#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
HOST_TEST_SOURCES = (
    PROJECT_DIR / "test" / "host" / "test_firmware_logic.cpp",
    PROJECT_DIR / "src" / "firmware_logic.cpp",
    PROJECT_DIR / "src" / "battery_curve.cpp",
    PROJECT_DIR / "src" / "local_button_logic.cpp",
    PROJECT_DIR / "src" / "serial_style.cpp",
    PROJECT_DIR / "src" / "runtime_config.cpp",
    PROJECT_DIR / "src" / "provisioning_logic.cpp",
)


def find_compiler() -> tuple[str, str]:
    for compiler in ("g++", "clang++"):
        found = shutil.which(compiler)
        if found:
            return found, "gnu"

    found = shutil.which("cl")
    if found:
        return found, "msvc"

    raise SystemExit(
        "C++ host tests require a compiler on PATH. Install one of: "
        "MSYS2 UCRT64 g++ (C:\\msys64\\ucrt64\\bin), "
        "MSYS2 CLANG64 clang++ (C:\\msys64\\clang64\\bin), "
        "or Visual Studio Build Tools cl.exe from a Developer PowerShell."
    )


def host_test_command(compiler: str, compiler_kind: str, binary: Path, tmpdir: Path) -> list[str]:
    if compiler_kind == "msvc":
        return [
            compiler,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            f"/I{PROJECT_DIR / 'src'}",
            *(str(source) for source in HOST_TEST_SOURCES),
            f"/Fe:{binary}",
            f"/Fo{tmpdir}\\",
        ]

    return [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(PROJECT_DIR / "src"),
        *(str(source) for source in HOST_TEST_SOURCES),
        "-o",
        str(binary),
    ]


def main() -> None:
    compiler, compiler_kind = find_compiler()

    with tempfile.TemporaryDirectory(prefix="esp32-meteo-host-tests-") as tmpdir:
        tmpdir_path = Path(tmpdir)
        binary_name = "test_firmware_logic.exe" if os.name == "nt" else "test_firmware_logic"
        binary = tmpdir_path / binary_name
        command = host_test_command(compiler, compiler_kind, binary, tmpdir_path)
        print(" ".join(command))
        subprocess.run(command, cwd=PROJECT_DIR, check=True)
        subprocess.run([str(binary)], cwd=PROJECT_DIR, check=True)
        print("Host firmware logic tests passed")


if __name__ == "__main__":
    main()
