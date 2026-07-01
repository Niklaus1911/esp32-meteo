#!/usr/bin/env python3
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)

PROJECT_DIR = Path(__file__).resolve().parents[1]

BUILD_ENVS = ("esp32dev", "esp32dev_ota", "esp32c3", "esp32c3_ota")
FORBIDDEN_TRACKED_PATHS = (
    "secrets.yaml",
    "src/secrets_local.h",
    "platformio.local.ini",
)
FORBIDDEN_TRACKED_DIRS = (".pio/",)

IDENTITY_CHECKS = (
    (
        "esp32dev",
        ("esp32-meteo-v3", "ESP32 Meteo V3", "esp32_meteo_v3"),
        ("esp32-meteo-c3", "ESP32 Meteo C3", "esp32_meteo_c3"),
    ),
    (
        "esp32dev_ota",
        ("esp32-meteo-v3", "ESP32 Meteo V3", "esp32_meteo_v3"),
        ("esp32-meteo-c3", "ESP32 Meteo C3", "esp32_meteo_c3"),
    ),
    (
        "esp32c3",
        ("esp32-meteo-c3", "ESP32 Meteo C3", "esp32_meteo_c3"),
        ("esp32-meteo-v3", "ESP32 Meteo V3", "esp32_meteo_v3"),
    ),
    (
        "esp32c3_ota",
        ("esp32-meteo-c3", "ESP32 Meteo C3", "esp32_meteo_c3"),
        ("esp32-meteo-v3", "ESP32 Meteo V3", "esp32_meteo_v3"),
    ),
)

TEXT_FILE_SUFFIXES = {".cpp", ".h", ".ini", ".md", ".py", ".yaml", ".yml"}
FORBIDDEN_TERMS = ("availability_topic", "expire_after", "offline", "last will")


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def run(command: list[str], description: str, *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    print(f"\n== {description} ==")
    print(" ".join(command))
    completed = subprocess.run(
        command,
        cwd=PROJECT_DIR,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if capture and completed.stdout:
        print(completed.stdout, end="")
    if completed.returncode != 0:
        fail(f"{description} failed with exit code {completed.returncode}")
    return completed


def git_lines(*args: str) -> list[str]:
    completed = subprocess.run(
        ["git", *args],
        cwd=PROJECT_DIR,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        if completed.stdout:
            print(completed.stdout, end="")
        fail(f"git {' '.join(args)} failed with exit code {completed.returncode}")
    return [line for line in completed.stdout.splitlines() if line]


def platformio_virtualenv_candidates() -> list[Path]:
    core_dirs: list[Path] = []
    configured_core_dir = os.environ.get("PLATFORMIO_CORE_DIR")
    if configured_core_dir:
        core_dirs.append(Path(configured_core_dir).expanduser())
    core_dirs.append(Path.home() / ".platformio")

    candidates: list[Path] = []
    for core_dir in core_dirs:
        candidates.extend(
            (
                core_dir / "penv" / "Scripts" / "pio.exe",
                core_dir / "penv" / "Scripts" / "platformio.exe",
                core_dir / "penv" / "bin" / "pio",
                core_dir / "penv" / "bin" / "platformio",
            )
        )

    unique_candidates: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = str(candidate)
        if key not in seen:
            unique_candidates.append(candidate)
            seen.add(key)
    return unique_candidates


def pio_command() -> str:
    for executable in ("pio", "platformio"):
        found = shutil.which(executable)
        if found:
            return found

    for candidate in platformio_virtualenv_candidates():
        if candidate.exists():
            return str(candidate)

    searched = ", ".join(str(candidate) for candidate in platformio_virtualenv_candidates())
    fail(
        "PlatformIO executable not found; add pio to PATH or use the PlatformIO virtualenv. "
        f"Searched: {searched}"
    )


def check_no_tracked_local_files() -> None:
    tracked = set(git_lines("ls-files"))
    failures = [path for path in FORBIDDEN_TRACKED_PATHS if path in tracked]
    failures.extend(
        path for path in tracked for forbidden_dir in FORBIDDEN_TRACKED_DIRS if path.startswith(forbidden_dir)
    )
    if failures:
        fail("local/generated files are tracked: " + ", ".join(sorted(failures)))
    print("No forbidden local/generated files are tracked")


def allow_documented_policy_line(path: str, line: str, term: str) -> bool:
    if not path.endswith(("README.md", "AGENTS.md")):
        return False

    normalized = " ".join(line.lower().split())
    if term in {"availability_topic", "expire_after"}:
        return "do not add" in normalized or "does not add" in normalized or "no `" in normalized
    if term == "offline":
        return "do not" in normalized or "does not" in normalized
    if term == "last will":
        return "do not" in normalized or "does not" in normalized
    return False


def check_forbidden_text() -> None:
    tracked_or_new_files = set(git_lines("ls-files"))
    tracked_or_new_files.update(git_lines("ls-files", "--others", "--exclude-standard"))
    tracked_text_files = [
        Path(path)
        for path in sorted(tracked_or_new_files)
        if Path(path).suffix in TEXT_FILE_SUFFIXES
        and not path.startswith("scripts/check_project.py")
        and (PROJECT_DIR / path).exists()
    ]

    violations: list[str] = []
    for relative_path in tracked_text_files:
        path = PROJECT_DIR / relative_path
        content = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(content.splitlines(), 1):
            lowered = line.lower()
            for term in FORBIDDEN_TERMS:
                if term in lowered and not allow_documented_policy_line(str(relative_path), line, term):
                    violations.append(f"{relative_path}:{line_number}: contains {term!r}")

        for match in re.finditer(r"mqtt\w*\.connect\s*\(([^;{}]*)\)", content, flags=re.IGNORECASE | re.DOTALL):
            argument_count = match.group(1).count(",") + 1
            if argument_count > 3:
                line_number = content.count("\n", 0, match.start()) + 1
                violations.append(f"{relative_path}:{line_number}: MQTT connect appears to include Last Will arguments")

    if violations:
        fail("forbidden MQTT/Home Assistant behavior found:\n" + "\n".join(violations))
    print("No forbidden MQTT/Home Assistant behavior found in tracked text files")


def build_all_environments() -> None:
    pio = pio_command()
    print(f"PlatformIO executable: {pio}")
    command = [pio, "run"]
    for environment in BUILD_ENVS:
        command.extend(("-e", environment))
    run(command, "PlatformIO build matrix")


def run_unit_tests() -> None:
    python_tests = sorted((PROJECT_DIR / "test").rglob("test_*.py"))
    if python_tests:
        run([sys.executable, "-m", "unittest", "discover", "-s", "test", "-p", "test_*.py"], "Python unit tests")
    else:
        print("\n== Python unit tests ==")
        print("No Python unit tests found; skipping")
    run([sys.executable, "scripts/run_host_tests.py"], "Host firmware logic tests")


def check_firmware_identities() -> None:
    print("\n== Firmware identity strings ==")
    for environment, required_strings, forbidden_strings in IDENTITY_CHECKS:
        firmware = PROJECT_DIR / ".pio" / "build" / environment / "firmware.elf"
        if not firmware.exists():
            fail(f"missing firmware artifact: {firmware.relative_to(PROJECT_DIR)}")

        data = firmware.read_bytes()
        missing = [value for value in required_strings if value.encode() not in data]
        forbidden = [value for value in forbidden_strings if value.encode() in data]
        if missing or forbidden:
            fail(
                f"{environment} identity mismatch; missing={missing or 'none'} "
                f"forbidden_present={forbidden or 'none'}"
            )
        print(f"{environment}: ok")


def main() -> None:
    run(["git", "diff", "--check"], "Unstaged whitespace check")
    run(["git", "diff", "--cached", "--check"], "Staged whitespace check")
    check_no_tracked_local_files()
    check_forbidden_text()
    run_unit_tests()
    build_all_environments()
    check_firmware_identities()
    print("\nProject check passed")


if __name__ == "__main__":
    main()
