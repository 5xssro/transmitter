#!/usr/bin/env python3
"""
Generuje TFT_eSPI smooth font (.vlw) s ASCII + slovenskou diakritikou.
Vyžaduje Pillow. Voliteľne: cesta k .ttf (inak Calibri / Arial z Windows).
Výstup: sd_assets/fonts/tx_ui14.vlw — skopíruj na SD do /fonts/
"""
from __future__ import annotations

import os
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Nainštaluj Pillow: python -m pip install Pillow", file=sys.stderr)
    sys.exit(1)

# Unicode bloky pre SK + bežná latinčina
EXTRA = (
    "áäčďéíĺľňóôŕšťúýž"
    "ÁÄČĎÉÍĹĽŇÓÔŔŠŤÚÝŽ"
    "öüÖÜ"  # náhodné nemecké
    "€µ"
)

def default_ttf() -> str:
    windir = os.environ.get("WINDIR", "C:\\Windows")
    for name in ("segoeui.ttf", "arial.ttf", "calibri.ttf"):
        p = os.path.join(windir, "Fonts", name)
        if os.path.isfile(p):
            return p
    return ""


def collect_chars() -> list[str]:
    chs: list[str] = []
    for o in range(32, 127):
        chs.append(chr(o))
    for c in EXTRA:
        if c not in chs:
            chs.append(c)
    return chs


def build_glyph(ch: str, font: ImageFont.FreeTypeFont, baseline_y: int, canvas_w: int, canvas_h: int) -> tuple:
    """Vráti (unicode, w, h, gxAdvance, gdY, gdX, alpha_bytes row-major)."""
    im = Image.new("L", (canvas_w, canvas_h), 0)
    dr = ImageDraw.Draw(im)
    try:
        dr.text((2, baseline_y), ch, font=font, fill=255, anchor="ls")
    except Exception:
        dr.text((2, baseline_y), ch, font=font, fill=255)

    bbox = im.getbbox()
    if bbox is None:
        u = ord(ch)
        adv = max(4, int(font.getlength(ch)) or 4)
        adv = min(255, adv)
        return u, 1, 1, adv, 1, 0, bytes([0])

    l, t, r, b = bbox
    bw, bh = max(1, r - l), max(1, b - t)
    crop = im.crop((l, t, r, b))
    raw = crop.tobytes()

    # gxAdvance: šírka kroku kurzora
    adv = int(max(font.getlength(ch), bw + 2))
    adv = max(3, min(adv, 255))
    bw = min(bw, 255)
    bh = min(bh, 255)

    gd_x = int(l - 2)  # okraj vľavo od „kurzora“
    gd_x = max(-128, min(127, gd_x))
    # gdY: baseline − horný okraj bitmapy (kladné = bitmapa nad baseline)
    gd_y = int(baseline_y - t)
    gd_y = max(-32768, min(32767, gd_y))

    return ord(ch), bh, bw, adv, gd_y, gd_x, raw


def write_vlw(path: str, glyphs: list[tuple], point_size: int, ascent: int, descent: int) -> None:
    """Formát podľa Bodmer TFT_eSPI Smooth_font.cpp (big-endian uint32 v hlavičke)."""
    gcount = len(glyphs)
    # Hlavička: 6 × BE uint32
    header = struct.pack(
        ">6I",
        gcount,
        11,  # version
        point_size,
        0,  # mboxY
        ascent,
        descent,
    )
    metric_blocks = bytearray()
    bitmap_offset = 24 + 28 * gcount
    bitmaps = bytearray()
    cur_off = bitmap_offset
    records = []
    for g in glyphs:
        u, h, w, adv, gd_y, gd_x, bmp = g
        if len(bmp) != w * h:
            raise SystemExit(f"bad bmp {u!r} len {len(bmp)} != {w*h}")
        metric_blocks.extend(
            struct.pack(
                ">7i",
                u,
                h,
                w,
                adv,
                gd_y,
                gd_x,
                0,
            )
        )
        records.append((cur_off, u))
        bitmaps.extend(bmp)
        cur_off += w * h
    
    name = b"tx_ui14"
    ps = b"TXUI14-Regular"
    # Trailer podľa Processing / TFT_eSPI: dĺžka menného reťazca (bez \0), potom \0 ukončený reťazec
    trailer = bytes([len(name)]) + name + b"\x00" + bytes([len(ps)]) + ps + b"\x00" + bytes([1])

    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(header)
        f.write(metric_blocks)
        f.write(bitmaps)
        f.write(trailer)
    print(f"OK: {path} ({gcount} znakov, ~{os.path.getsize(path)//1024} KiB)")


def main() -> None:
    ttf = sys.argv[1] if len(sys.argv) > 1 else default_ttf()
    if not ttf:
        print("Daj cestu k .ttf: python gen_tx_ui_vlw.py C:\\Windows\\Fonts\\arial.ttf", file=sys.stderr)
        sys.exit(1)

    out = os.path.join(os.path.dirname(__file__), "..", "sd_assets", "fonts", "tx_ui14.vlw")
    out = os.path.normpath(out)

    point_px = 18
    font = ImageFont.truetype(ttf, point_px)
    _ascent, _descent = font.getmetrics()
    chars = collect_chars()
    canvas_w, canvas_h = 200, 100
    # Baseline tak, aby sa zmestili znaky s diakritikou (Ascender z metrík)
    baseline_y = min(canvas_h - 8, max(24, _ascent + 8))

    # Hlavička VLW: ascent/descent v pixeloch blízko TrueType metrík (TFT_eSPI dopočíta yAdvance)
    ascent = max(12, min(80, _ascent + 4))
    descent = max(6, min(48, _descent + 6))
    glyphs: list[tuple] = []
    for ch in chars:
        glyphs.append(build_glyph(ch, font, baseline_y, canvas_w, canvas_h))

    write_vlw(out, glyphs, point_px, ascent, descent)


if __name__ == "__main__":
    main()
