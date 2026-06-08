import os, zlib, struct, sys

# Works both as PlatformIO extra_script and standalone: python convert_splash.py
try:
    Import("env")                           # noqa — SCons built-in
    _project_dir = env.subst("$PROJECT_DIR")
except NameError:
    _project_dir = os.path.dirname(os.path.abspath(__file__))

PNG_PATH    = os.path.join(_project_dir, "images", "T-REX-LOADING SCREEN.png")
HEADER_PATH = os.path.join(_project_dir, "t-rex-firmware", "ui", "splash", "splash_image.h")
TARGET_W, TARGET_H = 320, 240

# ── PNG decode (pure Python, no Pillow) ──────────────────────────────────────

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

def png_decode(data):
    assert data[:8] == b'\x89PNG\r\n\x1a\n', "Not a valid PNG"
    pos = 8
    idat, plte = [], []
    width = height = bit_depth = color_type = interlace = 0
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        tag    = data[pos+4:pos+8]
        body   = data[pos+8:pos+8+length]
        pos   += 12 + length
        if   tag == b'IHDR':
            width, height = struct.unpack('>II', body[:8])
            bit_depth, color_type, _, _, interlace = body[8], body[9], body[10], body[11], body[12]
        elif tag == b'PLTE':
            plte = [(body[i], body[i+1], body[i+2]) for i in range(0, len(body), 3)]
        elif tag == b'IDAT':
            idat.append(body)
        elif tag == b'IEND':
            break

    assert bit_depth  == 8, f"Only 8-bit PNG supported (got {bit_depth})"
    assert interlace  == 0, "Interlaced PNG not supported"

    ch = {0:1, 2:3, 3:1, 4:2, 6:4}[color_type]
    raw    = zlib.decompress(b''.join(idat))
    stride = width * ch
    pixels = []
    prev   = bytes(stride)
    ri     = 0

    for _ in range(height):
        ftype = raw[ri]; ri += 1
        row   = bytearray(raw[ri:ri + stride]); ri += stride
        if ftype == 1:    # Sub
            for x in range(ch, stride):
                row[x] = (row[x] + row[x - ch]) & 0xFF
        elif ftype == 2:  # Up
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 0xFF
        elif ftype == 3:  # Average
            for x in range(stride):
                a = row[x - ch] if x >= ch else 0
                row[x] = (row[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for x in range(stride):
                a = row[x - ch] if x >= ch else 0
                b = prev[x]
                c = prev[x - ch] if x >= ch else 0
                row[x] = (row[x] + _paeth(a, b, c)) & 0xFF
        # colour_type → RGB
        if   color_type == 2:  pixels += [(row[x*3],   row[x*3+1], row[x*3+2]) for x in range(width)]
        elif color_type == 6:  pixels += [(row[x*4],   row[x*4+1], row[x*4+2]) for x in range(width)]
        elif color_type == 0:  pixels += [(row[x],     row[x],     row[x])     for x in range(width)]
        elif color_type == 3:  pixels += [plte[row[x]]                          for x in range(width)]
        elif color_type == 4:  pixels += [(row[x*2],   row[x*2],   row[x*2])   for x in range(width)]
        prev = bytes(row)

    return width, height, pixels

# ── bilinear resize ───────────────────────────────────────────────────────────

def png_resize(pixels, sw, sh, dw, dh):
    out = []
    sx_scale = (sw - 1) / max(dw - 1, 1)
    sy_scale = (sh - 1) / max(dh - 1, 1)
    for dy in range(dh):
        gy = dy * sy_scale
        y0, y1 = int(gy), min(int(gy) + 1, sh - 1)
        vy = gy - y0
        row0 = pixels[y0 * sw:(y0 + 1) * sw]
        row1 = pixels[y1 * sw:(y1 + 1) * sw]
        for dx in range(dw):
            gx = dx * sx_scale
            x0, x1 = int(gx), min(int(gx) + 1, sw - 1)
            vx = gx - x0
            r = int(row0[x0][0] + vx*(row0[x1][0]-row0[x0][0]) + vy*(row1[x0][0]-row0[x0][0]) + vx*vy*(row0[x0][0]-row0[x1][0]-row1[x0][0]+row1[x1][0]))
            g = int(row0[x0][1] + vx*(row0[x1][1]-row0[x0][1]) + vy*(row1[x0][1]-row0[x0][1]) + vx*vy*(row0[x0][1]-row0[x1][1]-row1[x0][1]+row1[x1][1]))
            b = int(row0[x0][2] + vx*(row0[x1][2]-row0[x0][2]) + vy*(row1[x0][2]-row0[x0][2]) + vx*vy*(row0[x0][2]-row0[x1][2]-row1[x0][2]+row1[x1][2]))
            out.append((max(0,min(255,r)), max(0,min(255,g)), max(0,min(255,b))))
    return out

# ── PNG encode ────────────────────────────────────────────────────────────────

def png_encode(width, height, pixels):
    def mk_chunk(tag, body):
        hdr = tag + body
        return struct.pack('>I', len(body)) + hdr + struct.pack('>I', zlib.crc32(hdr) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: None
        for r, g, b in pixels[y * width:(y + 1) * width]:
            raw += bytes([r, g, b])

    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n'
            + mk_chunk(b'IHDR', ihdr)
            + mk_chunk(b'IDAT', zlib.compress(bytes(raw), 6))
            + mk_chunk(b'IEND', b''))

# ── main ──────────────────────────────────────────────────────────────────────
# Run manually:  python convert_splash.py
# As pre-script: only regenerates when PNG is newer than the existing header,
#                so repeated builds are a fast no-op.

_force = "--force" in sys.argv or "-f" in sys.argv

if not os.path.exists(PNG_PATH):
    print("WARNING: splash image not found, using existing header: " + PNG_PATH)
elif (not _force
      and os.path.exists(HEADER_PATH)
      and os.path.getmtime(HEADER_PATH) >= os.path.getmtime(PNG_PATH)):
    print("splash_image.h is up to date — skipping (run with --force to regenerate).")
else:
    print("Reading " + PNG_PATH + " ...")
    with open(PNG_PATH, 'rb') as f:
        raw_bytes = f.read()

    src_w, src_h, pixels = png_decode(raw_bytes)
    print(f"Decoded {src_w}x{src_h}, {len(pixels)} pixels")

    if src_w != TARGET_W or src_h != TARGET_H:
        print(f"Resizing to {TARGET_W}x{TARGET_H} (bilinear) ...")
        pixels = png_resize(pixels, src_w, src_h, TARGET_W, TARGET_H)

    png_bytes = png_encode(TARGET_W, TARGET_H, pixels)
    print(f"Re-encoded: {len(png_bytes)} bytes")

    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        f"// {TARGET_W}x{TARGET_H} splash — auto-generated by convert_splash.py",
        "static const uint8_t SPLASH_PNG[] = {",
    ]
    for i in range(0, len(png_bytes), 16):
        lines.append("    " + ", ".join("0x{:02X}".format(b) for b in png_bytes[i:i+16]) + ",")
    lines += [
        "};",
        f"static const size_t SPLASH_PNG_LEN = {len(png_bytes)};",
        "",
    ]

    with open(HEADER_PATH, "w") as f:
        f.write("\n".join(lines))

    print(f"splash_image.h written ({len(png_bytes)} bytes) -> {HEADER_PATH}")
