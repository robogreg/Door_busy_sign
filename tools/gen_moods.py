"""Generate tri-color mood bitmaps from OpenMoji.

OpenMoji (https://openmoji.org) by HfG Schwaebisch Gmuend, CC BY-SA 4.0.
Each emoji is reduced to the panel's three colors: dark outlines -> black,
red parts -> red, everything else (yellow faces, light fills) -> white.

Usage:
  python tools/gen_moods.py <cache_dir> [preview.png]

Downloads the 618 px PNGs for the moods below into <cache_dir> (reused if
present), writes firmware/src/display/mood_bitmaps.h, and optionally a preview
PNG of all 16 as they will look on the panel.
"""
import colorsys
import os
import sys
import urllib.request

from PIL import Image

OPENMOJI_TAG = "15.1.0"
URL = "https://raw.githubusercontent.com/hfg-gmuend/openmoji/{tag}/color/618x618/{code}.png"
SIZE = 112  # pixels; multiple of 8

# Order must match door::Mood (lib/core/status.h), starting at Happy.
MOODS = [
    ("happy", "1F600"),        # grinning face
    ("chill", "1F60E"),        # smiling face with sunglasses
    ("focused", "1F9D0"),      # face with monocle
    ("facepalm", "1F926"),     # person facepalming
    ("stressed", "1F630"),     # anxious face with sweat
    ("grumpy", "1F621"),       # pouting face (red)
    ("tired", "1F634"),        # sleeping face
    ("celebrating", "1F973"),  # partying face
    ("love", "1F60D"),         # smiling face with heart-eyes
    ("meh", "1F610"),          # neutral face
    ("sick", "1F912"),         # face with thermometer
    ("mindblown", "1F92F"),    # exploding head
    ("thinking", "1F914"),     # thinking face
    ("hungry", "1F924"),       # drooling face
    ("zen", "1F607"),          # smiling face with halo
    ("zipped", "1F910"),       # zipper-mouth face
]

BLACK, RED, WHITE = 0, 1, 2


def classify(r, g, b):
    """Black for outlines, red for any saturated accent color that is not a
    yellow/orange face or skin tone (reds, pinks, blues, greens), else white."""
    luma = 0.299 * r + 0.587 * g + 0.114 * b
    if luma < 80:
        return BLACK
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    hue = h * 360
    # 30..70 degrees keeps yellow faces and light-orange hands white while the
    # orange-red of the pouting face (about 24 degrees) still becomes red.
    if s > 0.35 and v > 0.35 and not (30 <= hue <= 70):
        return RED
    if luma < 120:
        return BLACK
    return WHITE


def load(cache, code):
    path = os.path.join(cache, code + ".png")
    if not os.path.exists(path):
        url = URL.format(tag=OPENMOJI_TAG, code=code)
        print("fetch", url)
        with urllib.request.urlopen(url, timeout=30) as resp:
            data = resp.read()
        with open(path, "wb") as f:
            f.write(data)
    img = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
    bg.alpha_composite(img)
    # OpenMoji art has padding; crop to content so the face fills the bitmap.
    box = Image.eval(bg.convert("L"), lambda v: 255 if v < 250 else 0).getbbox()
    if box:
        x0, y0, x1, y1 = box
        side = max(x1 - x0, y1 - y0)
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        half = side // 2 + 4
        bg = bg.crop((cx - half, cy - half, cx + half, cy + half))
    return bg.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)


def knockout(cls, share=0.8):
    """Black features sitting inside a red fill (the grumpy face) vanish on
    e-paper, whose red is a dark brick color. Each connected black shape whose
    surrounding pixels are at least `share` red is turned white. Outlines on
    the edge of a red area touch white on their other side, so they stay black."""
    out = [row[:] for row in cls]
    seen = [[False] * SIZE for _ in range(SIZE)]
    for sy in range(SIZE):
        for sx in range(SIZE):
            if cls[sy][sx] != BLACK or seen[sy][sx]:
                continue
            shape, stack, border = [], [(sx, sy)], {}
            seen[sy][sx] = True
            while stack:
                x, y = stack.pop()
                shape.append((x, y))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if not (0 <= nx < SIZE and 0 <= ny < SIZE):
                        border[(nx, ny)] = WHITE  # image edge counts as white
                    elif cls[ny][nx] == BLACK:
                        if not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
                    else:
                        border[(nx, ny)] = cls[ny][nx]
            if border and sum(c == RED for c in border.values()) >= share * len(border):
                for x, y in shape:
                    out[y][x] = WHITE
    return out


def planes(img):
    px = img.load()
    cls = knockout([[classify(*px[x, y]) for x in range(SIZE)] for y in range(SIZE)])
    black = bytearray(SIZE * SIZE // 8)
    red = bytearray(SIZE * SIZE // 8)
    for y in range(SIZE):
        for x in range(SIZE):
            i = (y * SIZE + x) // 8
            bit = 0x80 >> (x % 8)
            if cls[y][x] == BLACK:
                black[i] |= bit
            elif cls[y][x] == RED:
                red[i] |= bit
    return black, red, cls


def c_array(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("  " + ", ".join("0x%02x" % b for b in data[i : i + 16]) + ",")
    return "static const uint8_t %s[] PROGMEM = {\n%s\n};\n" % (name, "\n".join(lines))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cache = sys.argv[1]
    preview_path = sys.argv[2] if len(sys.argv) > 2 else None
    os.makedirs(cache, exist_ok=True)

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "firmware", "src", "display", "mood_bitmaps.h")
    parts = [
        "// Generated by tools/gen_moods.py. Do not edit.\n",
        "// Emoji art: OpenMoji %s (https://openmoji.org), CC BY-SA 4.0,\n" % OPENMOJI_TAG,
        "// reduced to black / red / white for the e-paper panel.\n",
        "#pragma once\n#include <Arduino.h>\n\n",
        "#define MOOD_BMP_SIZE %d\n\n" % SIZE,
    ]
    previews = []
    for mood, code in MOODS:
        img = load(cache, code)
        black, red, cls = planes(img)
        parts.append("// %s: U+%s\n" % (mood, code))
        parts.append(c_array("kMood_%s_k" % mood, black))
        parts.append(c_array("kMood_%s_r" % mood, red))
        previews.append(cls)

    parts.append("\nstruct MoodBitmap { const uint8_t* black; const uint8_t* red; };\n")
    parts.append("// Indexed by door::Mood - 1.\nstatic const MoodBitmap kMoodBitmaps[] = {\n")
    for mood, _ in MOODS:
        parts.append("  {kMood_%s_k, kMood_%s_r},\n" % (mood, mood))
    parts.append("};\n")
    with open(out, "w", newline="\n") as f:
        f.write("".join(parts))
    print("wrote", out)

    if preview_path:
        # 4 x 4 grid at 2x, with the same row backgrounds as the panel gallery.
        cell, scale = SIZE + 24, 2
        sheet = Image.new("RGB", (4 * cell * scale, 4 * cell * scale), (255, 255, 255))
        colors = {BLACK: (0, 0, 0), RED: (200, 30, 40), WHITE: (255, 255, 255)}
        row_bg = [(255, 255, 255), (200, 30, 40), (0, 0, 0), (255, 255, 255)]
        for n, cls in enumerate(previews):
            col, row = n % 4, n // 4
            tile = Image.new("RGB", (cell, cell), row_bg[row])
            disc = Image.new("L", (cell, cell), 0)
            from PIL import ImageDraw

            ImageDraw.Draw(disc).ellipse((4, 4, cell - 4, cell - 4), fill=255)
            tile.paste((255, 255, 255), mask=disc)
            off = (cell - SIZE) // 2
            for y in range(SIZE):
                for x in range(SIZE):
                    if cls[y][x] != WHITE:
                        tile.putpixel((off + x, off + y), colors[cls[y][x]])
            tile = tile.resize((cell * scale, cell * scale), Image.NEAREST)
            sheet.paste(tile, (col * cell * scale, row * cell * scale))
        sheet.save(preview_path)
        print("wrote", preview_path)


if __name__ == "__main__":
    main()
