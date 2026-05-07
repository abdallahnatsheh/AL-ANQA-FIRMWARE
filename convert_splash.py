Import("env")
import os

project_dir = env.subst("$PROJECT_DIR")
png_path    = os.path.join(project_dir, "images", "T-REX-LOADING SCREEN.png")
header_path = os.path.join(project_dir, "t-deck-cli", "splash_image.h")

if not os.path.exists(png_path):
    print("WARNING: splash image not found at: " + png_path)
else:
    with open(png_path, "rb") as f:
        data = f.read()

    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "static const uint8_t SPLASH_PNG[] = {",
    ]

    row = []
    for byte in data:
        row.append("0x{:02X}".format(byte))
        if len(row) == 16:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")

    lines.append("};")
    lines.append("static const size_t SPLASH_PNG_LEN = {};".format(len(data)))
    lines.append("")

    with open(header_path, "w") as f:
        f.write("\n".join(lines))

    print("Splash image embedded: {} bytes -> splash_image.h".format(len(data)))
