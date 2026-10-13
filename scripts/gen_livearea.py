#!/usr/bin/env python3
"""Generate VitaTouchPad LiveArea assets (icon0.png, bg.png, startup.png).

Run from the repo root:  python3 scripts/gen_livearea.py
Writes into assets/livearea/. Requires Pillow.
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "livearea")

ACCENT = (45, 170, 160)       # teal
ACCENT_DARK = (26, 115, 108)
WHITE = (255, 255, 255)


def vgrad(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def save_indexed(img, path):
    """Save as an 8-bit indexed (palette) PNG.

    The Vita package installer rejects LiveArea images unless they are
    indexed PNGs (error 0x8010113D otherwise), so quantize before saving.
    """
    img.convert("P", palette=Image.ADAPTIVE, colors=256).save(path, optimize=True)


def trackpad(draw, cx, cy, w, h, fill, alpha=255):
    """Rounded-rectangle trackpad glyph with a fingertip dot + ring."""
    d = draw
    x0, y0, x1, y1 = cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2
    # Pillow outline with width: draw border then inner dot
    d.rounded_rectangle([x0, y0, x1, y1], radius=int(h * 0.12), outline=fill, width=max(2, int(h * 0.05)))
    # fingertip dot, slightly above center
    r = h * 0.10
    fx, fy = cx, cy - h * 0.08
    d.ellipse([fx - r, fy - r, fx + r, fy + r], fill=fill)
    # radiating ring below-right (motion hint)
    rr = h * 0.20
    d.arc([fx - rr, fy - rr, fx + rr, fy + rr], start=300, end=60, fill=fill, width=max(2, int(h * 0.04)))


def dejavu(bold, size):
    path = "/usr/share/fonts/truetype/dejavu/%s.ttf" % ("DejaVuSans-Bold" if bold else "DejaVuSans")
    return ImageFont.truetype(path, size)


def gen_icon():
    size = 128
    img = vgrad(size, size, ACCENT, ACCENT_DARK)
    d = ImageDraw.Draw(img, "RGBA")
    d.rectangle([4, 4, size - 4, size - 4], outline=(255, 255, 255, 60), width=2)
    trackpad(d, 64, 66, 84, 60, WHITE)
    save_indexed(img, os.path.join(OUT, "icon0.png"))
    print("icon0.png")


def gen_bg():
    w, h = 840, 500
    img = vgrad(w, h, ACCENT, ACCENT_DARK)
    d = ImageDraw.Draw(img, "RGBA")
    # faint watermark trackpad, right side
    wm = Image.new("RGBA", (420, 420), (0, 0, 0, 0))
    wd = ImageDraw.Draw(wm)
    trackpad(wd, 210, 210, 300, 220, (255, 255, 255))
    # Pillow has no per-draw alpha on RGBA overlay without mask games; paste dim
    dim = Image.new("RGBA", wm.size, (0, 0, 0, 0))
    # cheap fade: blend watermark toward transparent
    wm = Image.blend(Image.new("RGBA", wm.size, (0, 0, 0, 0)), wm, 0.12)
    img.paste(wm, (w - 380, 60), wm)
    font = dejavu(True, 84)
    txt = "VitaTouchPad"
    tw = d.textlength(txt, font=font)
    d.text(((w - tw) / 2, 190), txt, font=font, fill=WHITE)
    sub = dejavu(False, 34)
    tag = "Touch. Move. Click."
    tw2 = d.textlength(tag, font=sub)
    d.text(((w - tw2) / 2, 300), tag, font=sub, fill=(255, 255, 255, 220))
    save_indexed(img, os.path.join(OUT, "bg.png"))
    print("bg.png")


def gen_startup():
    w, h = 280, 158
    img = vgrad(w, h, ACCENT, ACCENT_DARK)
    d = ImageDraw.Draw(img, "RGBA")
    trackpad(d, 48, 79, 64, 72, WHITE)
    font = dejavu(True, 30)
    txt = "VitaTouchPad"
    tw = d.textlength(txt, font=font)
    d.text(((w - tw) / 2 + 24, 64), txt, font=font, fill=WHITE)
    save_indexed(img, os.path.join(OUT, "startup.png"))
    print("startup.png")


def gen_template():
    with open(os.path.join(OUT, "template.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n\n')
        f.write('<livearea style="a1" format-ver="01.00" content-rev="1">\n')
        f.write('\t<livearea-background>\n')
        f.write('\t\t<image>bg.png</image>\n')
        f.write('\t</livearea-background>\n')
        f.write('\n')
        f.write('\t<gate>\n')
        f.write('\t\t<startup-image>startup.png</startup-image>\n')
        f.write('\t</gate>\n')
        f.write('</livearea>\n')
    print("template.xml")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    gen_icon()
    gen_bg()
    gen_startup()
    gen_template()
