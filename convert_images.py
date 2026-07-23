#!/usr/bin/env python3

import os
import sys
import re

def process_file(filename, resize_to=None):
    basename = os.path.basename(filename)
    varname = re.sub(r'[^a-zA-Z0-9_]', '_', os.path.splitext(basename)[0])

    if resize_to:
        try:
            from PIL import Image
            import io
            img = Image.open(filename).convert("RGBA")
            img = img.resize((resize_to, resize_to), Image.Resampling.LANCZOS)
            buf = io.BytesIO()
            img.save(buf, format="PNG")
            data = buf.getvalue()
        except Exception as e:
            print(f"Warning: menu resize failed for {basename} ({e}), using raw PNG")
            with open(filename, 'rb') as f:
                data = f.read()
    else:
        with open(filename, 'rb') as f:
            data = f.read()

    output = f"// Datas for {basename}\n"
    output += f"static const unsigned char {varname}_data[] = {{\n    "

    byte_count = 0
    for byte in data:
        output += f"0x{byte:02x}, "
        byte_count += 1
        if byte_count % 12 == 0:
            output += "\n    "

    output += "\n};\n"
    output += f"static const size_t {varname}_size = sizeof({varname}_data);\n\n"

    return output

# Menu icons: pre-scale at build time for cleaner TQt3 rendering.
MENU_ICON_SIZES = {
    "backlight.png": 24,
    "eco.png": 24,
    "perf.png": 24,
    "presmode.png": 24,
    "powernap.png": 24,
    "info.png": 24,
    "history.png": 24,
    "settings.png": 24,
    "charge.png": 24,
    "warn.png": 24,
    "yabatman_crit.png": 24,
    "check.png": 14,
}

def generate_header_file(directory):
    pattern = re.compile(r'battery-level-.*-symbolic\.png')
    image_files = [os.path.join(directory, f) for f in os.listdir(directory)
                   if pattern.match(f) or f in ("yabatman_ac.png", "yabatman_bat.png","yabatman_crit.png","backlight.png","settings.png","transparent_icon.png","pres.png","media.png","warn.png","history.png","info.png","charge.png", "uncharge.png","powernap.png","yabatman.png","presmode.png","eco.png","perf.png","iswifi.png","check.png", "health.png", "indicators.png", "model.png", "times.png")]

    if not image_files:
        print(f"no icons found in {directory}")
        return None

    header_content = "/* header file auto generated for icons */\n"
    header_content += "#ifndef BATTERY_ICONS_H\n"
    header_content += "#define BATTERY_ICONS_H\n\n"
    header_content += "#include <stddef.h>\n\n"

    for file in sorted(image_files):
        basename = os.path.basename(file)
        resize_to = MENU_ICON_SIZES.get(basename)
        header_content += process_file(file, resize_to=resize_to)

    header_content += "#endif /* BATTERY_ICONS_H */\n"

    with open("src/battery_icons.h", "w") as f:
        f.write(header_content)

    print(f"src/battery_icons.h generated: {len(image_files)} icons (raw PNG bytes)")
    return "src/battery_icons.h"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 convert_images.py /path/to/icons/")
        sys.exit(1)

    icon_dir = sys.argv[1]
    if not os.path.isdir(icon_dir):
        print(f"Folder {icon_dir} doesn't exist")
        sys.exit(1)

    generate_header_file(icon_dir)
