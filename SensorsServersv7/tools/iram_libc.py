# Classic ESP32 turns the flash cache off while programming the radio and
# reading the flash id. The prebuilt linker script keeps newlib in internal RAM
# but leaves Wi-Fi .wifi0iram in flash, so those calls panic. This writes a
# script that keeps both in internal RAM and puts it ahead of the package script.
Import("env")
import os

def write_sdkconfig_header(defaults_path, header_path):
    lines = [
        "/* Generated from sdkconfig.defaults. Do not edit. */",
        "#pragma once",
    ]
    with open(defaults_path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if not key.startswith("CONFIG_"):
                continue
            if value == "y":
                rendered = "1"
            elif value == "n":
                continue
            else:
                rendered = value
            lines.append("#define %s %s" % (key, rendered))
    os.makedirs(os.path.dirname(header_path), exist_ok=True)
    with open(header_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")

board = env.BoardConfig()
if board.get("build.mcu", "") == "esp32":
    package = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")
    flash_mode = board.get("build.flash_mode", "dio")
    memory_type = board.get("build.arduino.memory_type", flash_mode + "_qspi")
    header = os.path.join(package, "esp32", memory_type, "include", "sdkconfig.h")
    defaults = os.path.join(env.subst("$PROJECT_DIR"), "sdkconfig.defaults")
    if os.path.isfile(defaults):
        write_sdkconfig_header(defaults, header)
    build_dir = env.subst("$BUILD_DIR")
    out_dir = os.path.join(build_dir, "ld_iram")
    os.makedirs(out_dir, exist_ok=True)
    source = os.path.join(package, "esp32", "ld", "sections.ld")
    dest = os.path.join(out_dir, "sections.ld")
    with open(source, "r", encoding="utf-8") as handle:
        text = handle.read()
    flash_rule = "    *(.wifi0iram .wifi0iram.*)\n"
    iram_rule = (
        "    *libnet80211.a:(.wifi0iram .wifi0iram.*)\n"
        "    *libpp.a:(.wifi0iram .wifi0iram.*)\n"
    )
    if flash_rule not in text or text.count(iram_rule) == 0:
        text = text.replace(flash_rule, "", 1)
        anchor = "    *libxtensa.a:(EXCLUDE_FILE(*libxtensa.a:xt_trax.*"
        text = text.replace(anchor, iram_rule + anchor, 1)
        with open(dest, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    env.Prepend(LIBPATH=[out_dir])
