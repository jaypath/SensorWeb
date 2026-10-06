"""Point every espota upload at tools/espota.py (30s per-block ACK timeout)."""

import os
from pathlib import Path

Import("env")

wrapper = Path(env["PROJECT_DIR"]) / "tools" / "espota.py"
uploader = str(env.get("UPLOADER") or "")
if uploader:
    uploader_path = Path(uploader)
    if uploader_path.name == "espota.py" and uploader_path.resolve() != wrapper.resolve():
        os.environ["ESPOTA_REAL"] = str(uploader_path)
        env.Replace(UPLOADER=str(wrapper))
