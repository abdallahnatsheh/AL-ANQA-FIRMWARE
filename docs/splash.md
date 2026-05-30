---
title: Custom Splash Screen
parent: System
nav_order: 10
---

# Custom Splash Screen

T-Rex shows a full-screen image when it boots. You can replace it with any image — your logo, a custom design, anything you like. No code changes needed; just drop in a PNG and run one script.

---

## How It Works

The splash screen is stored as a raw PNG byte array embedded directly in the firmware (`splash_image.h`). The `convert_splash.py` script converts any PNG to that format automatically, handling resizing and encoding.

At boot, the firmware reads the byte array and renders it pixel-by-pixel on the 320×240 ST7789 display before the command prompt appears.

---

## Replacing the Splash Screen

**Step 1 — Prepare your image**

Place your PNG at:
```
images/T-REX-LOADING SCREEN.png
```

Any size works — the script bilinear-resizes it to **320×240** automatically. For the sharpest result, start with an image that has a 4:3 ratio (e.g. 1280×960, 1920×1440).

Color mode: RGB, RGBA, greyscale, or palette — all supported. Alpha channel is dropped (transparent pixels become black).

**Step 2 — Run the converter**

```bash
python convert_splash.py
```

Output:
```
Reading images/T-REX-LOADING SCREEN.png ...
Decoded 1448x1086, 1572528 pixels
Resizing to 320x240 (bilinear) ...
Re-encoded: 80006 bytes
splash_image.h written (80006 bytes) -> t-rex-firmware/splash_image.h
```

**Step 3 — Flash**

Build and upload normally in VSCode / PlatformIO. The new image is embedded in the firmware.

---

## Force Regenerate

The script skips regeneration if `splash_image.h` is already newer than the PNG (fast no-op on repeated builds). To force a rebuild:

```bash
python convert_splash.py --force
python convert_splash.py -f     # same
```

---

## Restore the Default Splash

The default splash (`images/T-REX-LOADING SCREEN.png`) is included in the repo. To restore it after swapping it out:

```bash
git checkout images/"T-REX-LOADING SCREEN.png"
python convert_splash.py --force
```

---

## Technical Details

| Property | Value |
|----------|-------|
| Output size | 320×240 pixels |
| Color depth | 16-bit RGB565 (via PNG re-encode) |
| Resize algorithm | Bilinear interpolation (pure Python, no Pillow) |
| Output format | C `uint8_t` array in `t-rex-firmware/splash_image.h` |
| Build impact | Script runs as PlatformIO pre-build hook but skips if header is up to date — no slowdown on repeat builds |

The converter is pure Python (no external dependencies — uses only `os`, `zlib`, `struct`, `sys` from the standard library). It works on any machine that can run PlatformIO.

---

## Notes

- The script runs automatically on the **first** build (or when the PNG is newer than the header). Subsequent builds skip it entirely.
- `splash_image.h` is committed to the repo so fresh clones build immediately without needing to run the script first.
- The splash displays for ~1.5 seconds during boot before the command prompt appears.
