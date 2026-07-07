#!/usr/bin/env python3
"""Build → flash (auto port) → 30s monitor with log."""
import subprocess, sys, time, os
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent

IDF_PATH = Path("C:/esp/v6.0/esp-idf")
IDF_PYTHON = Path("C:/Espressif/tools/python_env/idf6.0_py3.14_env/Scripts/python.exe")

sys.path.insert(0, str(Path(__file__).parent))
from monitor import monitor_port
from find_port import find_esp32_port

def clean_env():
    tools = Path("C:/Espressif/tools")
    env = os.environ.copy()
    for k in ("MSYSTEM", "MSYS", "MINGW_PREFIX"):
        env.pop(k, None)
    env["IDF_TOOLS_PATH"] = str(tools)
    env["IDF_PATH"] = str(IDF_PATH)
    env["ESP_IDF_VERSION"] = "6.0"
    env["IDF_PYTHON_ENV_PATH"] = str(IDF_PYTHON.parent.parent)
    extra = [
        str(tools / "idf-exe/1.0.3"),
        str(tools / "cmake/4.0.3/bin"),
        str(tools / "ninja/1.12.1"),
        str(tools / "python_env/idf6.0_py3.14_env/Scripts"),
        str(tools / "xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin"),
    ]
    env["PATH"] = ";".join(extra + [env.get("PATH", "")])
    return env

def log(msg):
    print(f"  {msg}", flush=True)

def idf(action, *args):
    cmd = [str(IDF_PYTHON), str(IDF_PATH / "tools/idf.py"), action, *args]
    return subprocess.run(cmd, env=clean_env())

def main():
    os.chdir(PROJECT_DIR)

    log("Build...")
    r = idf("build")
    if r.returncode: sys.exit(1)

    port = find_esp32_port()
    if not port:
        log("No ESP32 port found")
        sys.exit(1)
    log(f"Port: {port}")

    log("Flash...")
    r = idf("-p", port, "flash")
    if r.returncode: sys.exit(1)

    time.sleep(2)
    sys.exit(monitor_port(port, timeout=30, log_dir=PROJECT_DIR / "logs", no_reset=True))

if __name__ == "__main__":
    main()
