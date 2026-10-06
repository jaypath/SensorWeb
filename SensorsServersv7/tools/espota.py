"""OTA upload wrapper: wait 30s for each block acknowledgement.

PlatformIO invokes this instead of the stock espota.py. The stock script
waits 10 seconds for the device to acknowledge each 1024-byte chunk. Flash
writes plus on-device progress updates can take longer than that, so this
wrapper runs the installed script with that per-block wait set to 30s.
"""

import os
import sys
from pathlib import Path

BLOCK_ACK_TIMEOUT_S = 30
_NEEDLE = "connection.settimeout(10)"


def find_stock_espota():
    here = Path(__file__).resolve()
    env_path = os.environ.get("ESPOTA_REAL", "").strip().strip('"')
    if env_path:
        candidate = Path(env_path)
        if candidate.is_file() and candidate.resolve() != here:
            return candidate

    packages = Path.home() / ".platformio" / "packages"
    candidates = [
        packages / "tool-espotapy" / "espota.py",
        packages / "framework-arduinoespressif32" / "tools" / "espota.py",
        packages / "framework-arduinoespressif32-libs" / "tools" / "espota.py",
    ]
    for candidate in candidates:
        if candidate.is_file() and candidate.resolve() != here:
            return candidate

    if packages.is_dir():
        for candidate in packages.rglob("espota.py"):
            if candidate.resolve() != here:
                return candidate
    return None


def main():
    stock = find_stock_espota()
    if stock is None:
        sys.stderr.write("ERROR: stock espota.py not found under .platformio\\packages\n")
        return 1

    src = stock.read_text(encoding="utf-8")
    if src.count(_NEEDLE) != 1:
        sys.stderr.write(
            "ERROR: %s does not have exactly one per-block connection.settimeout(10)\n" % stock
        )
        return 1

    src = src.replace(_NEEDLE, "connection.settimeout(%d)" % BLOCK_ACK_TIMEOUT_S, 1)
    sys.stderr.write("OTA per-block timeout: %ds\n" % BLOCK_ACK_TIMEOUT_S)
    sys.stderr.flush()
    exec(compile(src, str(stock), "exec"), {"__name__": "__main__", "__file__": str(stock)})
    return 0


if __name__ == "__main__":
    main()
