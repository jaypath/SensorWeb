"""
Pre-build patch for LovyanGFX: fix gpio_hal_iomux_func_sel for Arduino ESP32 core 3.3.x.

With pioarduino 55.03.35 (Arduino core 3.3.x / ESP-IDF 5.5), the HAL renamed
gpio_hal_iomux_func_sel -> gpio_hal_func_sel. LovyanGFX still calls the old name
in Bus_RGB.cpp, causing: 'gpio_hal_iomux_func_sel' was not declared in this scope.

This script replaces the call in the installed library so builds succeed.
Run automatically before build via platformio.ini extra_scripts.
"""

import glob
import os
from pathlib import Path

def main():
    # .pio/libdeps/<env>/LovyanGFX/.../Bus_RGB.cpp
    pio = Path(".pio")
    if not pio.is_dir():
        return
    libdeps = pio / "libdeps"
    if not libdeps.is_dir():
        return

    pattern = str(libdeps / "*" / "LovyanGFX" / "src" / "lgfx" / "v1" / "platforms" / "esp32s3" / "Bus_RGB.cpp")
    for path in glob.glob(pattern):
        path = Path(path)
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if "gpio_hal_iomux_func_sel" not in text:
            continue
        new_text = text.replace("gpio_hal_iomux_func_sel", "gpio_hal_func_sel")
        if new_text != text:
            path.write_text(new_text, encoding="utf-8")
            print("Patched LovyanGFX Bus_RGB.cpp: gpio_hal_iomux_func_sel -> gpio_hal_func_sel")

if __name__ == "__main__":
    main()
