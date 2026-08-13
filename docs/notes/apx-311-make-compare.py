#!/usr/bin/env python3
"""APX-311 evidence: crop metrics + comparison plates (not part of the product)."""

from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageChops, ImageEnhance, ImageOps

ROOT = Path(__file__).resolve().parent
NOW = ROOT / "apx-311-text-screenshot/text-screenshot/msdf"
OLD = ROOT / "apx-269-text-screenshot/text-screenshot/msdf"
FT = ROOT / "apx-269-text-screenshot/text-screenshot/freetype"
OUT = ROOT / "apx-311-text-screenshot/compare"
OUT.mkdir(parents=True, exist_ok=True)

# Rec. 601 luminance
def luma(px):
    r, g, b = px[:3]
    return 0.299 * r + 0.587 * g + 0.114 * b


def load(p: Path) -> Image.Image:
    return Image.open(p).convert("RGB")


def crop(im: Image.Image, box):
    return im.crop(box)


def ink_stats(im: Image.Image, bg_sample_box=None):
    """Ink = pixels whose luma differs from local background by > 8."""
    px = im.load()
    w, h = im.size
    if bg_sample_box is None:
        # top-left 4x4 mean as background
        acc = 0.0
        n = 0
        for y in range(min(4, h)):
            for x in range(min(4, w)):
                acc += luma(px[x, y])
                n += 1
        bg = acc / max(n, 1)
    else:
        x0, y0, x1, y1 = bg_sample_box
        acc = 0.0
        n = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                acc += luma(px[x, y])
                n += 1
        bg = acc / max(n, 1)

    ink = 0
    peak = 0.0
    mean_acc = 0.0
    rgb_acc = [0.0, 0.0, 0.0]
    max_rg = 0
    max_rb = 0
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            L = luma(p)
            if abs(L - bg) > 8.0:
                ink += 1
                peak = max(peak, L)
                mean_acc += L
                rgb_acc[0] += p[0]
                rgb_acc[1] += p[1]
                rgb_acc[2] += p[2]
                max_rg = max(max_rg, abs(p[0] - p[1]))
                max_rb = max(max_rb, abs(p[0] - p[2]))
    mean_L = (mean_acc / ink) if ink else 0.0
    mean_rgb = tuple(c / ink for c in rgb_acc) if ink else (0, 0, 0)
    return {
        "ink": ink,
        "peakL": peak,
        "meanL": mean_L,
        "meanRGB": mean_rgb,
        "max_rg": max_rg,
        "max_rb": max_rb,
        "bg": bg,
        "w": w,
        "h": h,
    }


def nearest_up(im: Image.Image, scale: int) -> Image.Image:
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def label_plate(im: Image.Image, text: str) -> Image.Image:
    pad = 18
    out = Image.new("RGB", (im.width, im.height + pad), (18, 20, 24))
    out.paste(im, (0, pad))
    d = ImageDraw.Draw(out)
    d.text((4, 2), text, fill=(220, 220, 230))
    return out


def hstack(imgs, gap=6, bg=(12, 14, 18)):
    h = max(i.height for i in imgs)
    w = sum(i.width for i in imgs) + gap * (len(imgs) - 1)
    out = Image.new("RGB", (w, h), bg)
    x = 0
    for i in imgs:
        out.paste(i, (x, 0))
        x += i.width + gap
    return out


def vstack(imgs, gap=6, bg=(12, 14, 18)):
    w = max(i.width for i in imgs)
    h = sum(i.height for i in imgs) + gap * (len(imgs) - 1)
    out = Image.new("RGB", (w, h), bg)
    y = 0
    for i in imgs:
        out.paste(i, (0, y))
        y += i.height + gap
    return out


def diff_x(a: Image.Image, b: Image.Image, scale=2) -> Image.Image:
    a = a.convert("RGB")
    b = b.convert("RGB")
    if a.size != b.size:
        b = b.resize(a.size, Image.NEAREST)
    d = ImageChops.difference(a, b)
    # amplify
    enh = ImageEnhance.Brightness(d)
    d = enh.enhance(4.0)
    return nearest_up(d, scale)


def trio(now, ft, old, labels, scale=4):
    plates = [
        label_plate(nearest_up(now, scale), labels[0]),
        label_plate(nearest_up(ft, scale), labels[1]),
        label_plate(nearest_up(old, scale), labels[2]),
    ]
    return hstack(plates)


def first_ink_span(im: Image.Image, y0, y1):
    px = im.load()
    w, h = im.size
    xmin, xmax = w, -1
    ymin, ymax = h, -1
    for y in range(y0, min(y1, h)):
        for x in range(w):
            if luma(px[x, y]) > 40:
                xmin = min(xmin, x)
                xmax = max(xmax, x)
                ymin = min(ymin, y)
                ymax = max(ymax, y)
    return xmin, ymin, xmax, ymax


def main():
    now_sizes = load(NOW / "sizes.png")
    ft_sizes = load(FT / "sizes.png")
    old_sizes = load(OLD / "sizes.png")
    now_pang = load(NOW / "pangram.png")
    ft_pang = load(FT / "pangram.png")
    old_pang = load(OLD / "pangram.png")
    now_col = load(NOW / "colors_alpha.png")
    ft_col = load(FT / "colors_alpha.png")
    old_col = load(OLD / "colors_alpha.png")
    now_sc = load(NOW / "scaled.png")
    ft_sc = load(FT / "scaled.png")
    old_sc = load(OLD / "scaled.png")
    now_gr = load(NOW / "glyph_grid.png")
    ft_gr = load(FT / "glyph_grid.png")
    old_gr = load(OLD / "glyph_grid.png")
    now_at = load(NOW / "atlas_msdf.png")
    old_at = load(OLD / "atlas_msdf.png")

    # Scene-derived crops (640x480, label tops from harness)
    crops = {
        "sizes_8px": (12, 8, 220, 32),
        "sizes_12px": (12, 40, 280, 70),
        "sizes_20px": (12, 76, 360, 116),
        "sizes_48_The": (12, 120, 220, 188),
        "sizes_96_T": (12, 192, 90, 300),
        "sizes_96_brown": (12, 300, 420, 410),
        "pangram_head": (16, 16, 260, 52),
        "pangram_punct": (16, 36, 420, 72),
        "colors_white": (28, 24, 200, 52),
        "colors_yellow": (340, 24, 520, 52),
        "colors_magenta": (340, 64, 560, 94),
        "colors_green": (28, 160, 420, 200),
        "scaled_28px": (16, 16, 520, 56),
        "scaled_6px": (16, 208, 280, 236),
        "grid_row0": (12, 8, 260, 34),
        "grid_last": (12, 138, 280, 170),
        "atlas_tl": (0, 0, 80, 80),
        "atlas_mid": (88, 88, 168, 168),
    }

    # Full-frame side-by-sides at 1x (now | ft | old)
    def sbs3(a, b, c, name):
        plate = hstack(
            [
                label_plate(a, "now MSDF (APX-311)"),
                label_plate(b, "FreeType ref (APX-269)"),
                label_plate(c, "pre-fix MSDF (APX-269)"),
            ]
        )
        plate.save(OUT / f"{name}_side_by_side.png")

    sbs3(now_sizes, ft_sizes, old_sizes, "sizes")
    sbs3(now_pang, ft_pang, old_pang, "pangram")
    sbs3(now_col, ft_col, old_col, "colors_alpha")
    sbs3(now_sc, ft_sc, old_sc, "scaled")
    sbs3(now_gr, ft_gr, old_gr, "glyph_grid")

    # Diffs now vs ft and now vs old
    for name, a, b, c in (
        ("sizes", now_sizes, ft_sizes, old_sizes),
        ("pangram", now_pang, ft_pang, old_pang),
        ("colors_alpha", now_col, ft_col, old_col),
        ("scaled", now_sc, ft_sc, old_sc),
        ("glyph_grid", now_gr, ft_gr, old_gr),
    ):
        diff_x(a, b, 2).save(OUT / f"{name}_diff_vs_ft_x2.png")
        diff_x(a, c, 2).save(OUT / f"{name}_diff_vs_old_x2.png")

    # Zooms / trios
    trio_specs = [
        ("sizes_8px_trio", "sizes", "sizes_8px", 6),
        ("sizes_12px_trio", "sizes", "sizes_12px", 5),
        ("sizes_20px_trio", "sizes", "sizes_20px", 4),
        ("sizes_48_The_trio", "sizes", "sizes_48_The", 3),
        ("sizes_96_T_trio", "sizes", "sizes_96_T", 3),
        ("sizes_96_brown_trio", "sizes", "sizes_96_brown", 2),
        ("pangram_head_trio", "pangram", "pangram_head", 4),
        ("pangram_punct_trio", "pangram", "pangram_punct", 3),
        ("colors_yellow_trio", "colors", "colors_yellow", 4),
        ("colors_white_trio", "colors", "colors_white", 4),
        ("colors_magenta_trio", "colors", "colors_magenta", 4),
        ("colors_green_trio", "colors", "colors_green", 3),
        ("scaled_28px_trio", "scaled", "scaled_28px", 3),
        ("scaled_6px_trio", "scaled", "scaled_6px", 6),
        ("grid_row0_trio", "grid", "grid_row0", 4),
        ("grid_last_trio", "grid", "grid_last", 4),
    ]
    srcs = {
        "sizes": (now_sizes, ft_sizes, old_sizes),
        "pangram": (now_pang, ft_pang, old_pang),
        "colors": (now_col, ft_col, old_col),
        "scaled": (now_sc, ft_sc, old_sc),
        "grid": (now_gr, ft_gr, old_gr),
    }
    for fname, key, crop_name, scale in trio_specs:
        box = crops[crop_name]
        a, b, c = srcs[key]
        plate = trio(
            crop(a, box),
            crop(b, box),
            crop(c, box),
            (f"now {crop_name}", f"FT {crop_name}", f"old {crop_name}"),
            scale=scale,
        )
        plate.save(OUT / f"{fname}.png")
        nearest_up(crop(a, box), scale).save(OUT / f"msdf_{crop_name}_zoom.png")
        nearest_up(crop(b, box), scale).save(OUT / f"freetype_{crop_name}_zoom.png")
        nearest_up(crop(c, box), scale).save(OUT / f"old_{crop_name}_zoom.png")

    # Atlas zooms
    nearest_up(crop(now_at, crops["atlas_tl"]), 6).save(OUT / "atlas_msdf_tl_zoom.png")
    nearest_up(crop(now_at, crops["atlas_mid"]), 6).save(OUT / "atlas_msdf_mid_zoom.png")
    nearest_up(crop(old_at, crops["atlas_tl"]), 6).save(OUT / "atlas_old_tl_zoom.png")
    hstack(
        [
            label_plate(nearest_up(crop(now_at, crops["atlas_tl"]), 5), "now atlas TL"),
            label_plate(nearest_up(crop(old_at, crops["atlas_tl"]), 5), "old atlas TL"),
        ]
    ).save(OUT / "atlas_tl_compare.png")

    # Metrics table
    metric_crops = [
        ("sizes 8 px", "sizes", "sizes_8px"),
        ("sizes 12 px", "sizes", "sizes_12px"),
        ("sizes 20 px", "sizes", "sizes_20px"),
        ("sizes 48 The", "sizes", "sizes_48_The"),
        ("sizes 96 T", "sizes", "sizes_96_T"),
        ("sizes 96 brown", "sizes", "sizes_96_brown"),
        ("pangram head", "pangram", "pangram_head"),
        ("pangram whole", "pangram", None),
        ("yellow on blue", "colors", "colors_yellow"),
        ("scaled 28 px", "scaled", "scaled_28px"),
        ("scaled 6 px", "scaled", "scaled_6px"),
    ]
    print("CROP | now ink/peak/mean | FT | old")
    for title, key, cname in metric_crops:
        a, b, c = srcs[key]
        if cname:
            box = crops[cname]
            sa, sb, sc = ink_stats(crop(a, box)), ink_stats(crop(b, box)), ink_stats(crop(c, box))
        else:
            sa, sb, sc = ink_stats(a), ink_stats(b), ink_stats(c)
        print(
            f"{title}: now {sa['ink']}/{sa['peakL']:.1f}/{sa['meanL']:.1f} RGB{tuple(round(x,1) for x in sa['meanRGB'])} "
            f"| FT {sb['ink']}/{sb['peakL']:.1f}/{sb['meanL']:.1f} RGB{tuple(round(x,1) for x in sb['meanRGB'])} "
            f"| old {sc['ink']}/{sc['peakL']:.1f}/{sc['meanL']:.1f} RGB{tuple(round(x,1) for x in sc['meanRGB'])}"
        )

    # Channel split on 96 T
    box = crops["sizes_96_T"]
    for name, im in (("now", now_sizes), ("FT", ft_sizes), ("old", old_sizes)):
        s = ink_stats(crop(im, box))
        print(f"96T {name} max|R-G|={s['max_rg']} max|R-B|={s['max_rb']}")

    # Layout: first ink row of glyph_grid
    for name, im in (("now", now_gr), ("FT", ft_gr), ("old", old_gr)):
        span = first_ink_span(im, 0, 40)
        print(f"grid first-ink {name}: x={span[0]}..{span[2]} (span {span[2]-span[0]}) y={span[1]}..{span[3]}")

    # Atlas unused texel histogram buckets
    def atlas_hist(im):
        px = im.load()
        w, h = im.size
        black = mid = other = 0
        for y in range(h):
            for x in range(w):
                p = px[x, y]
                if p[0] < 8 and p[1] < 8 and p[2] < 8:
                    black += 1
                elif abs(p[0] - p[1]) < 6 and abs(p[0] - p[2]) < 6 and 110 < p[0] < 150:
                    mid += 1
                else:
                    other += 1
        n = w * h
        return black / n, mid / n, other / n, w, h

    print("atlas now black/mid/other", atlas_hist(now_at))
    print("atlas old black/mid/other", atlas_hist(old_at))

    # Horizontal fringe count through 96 T stem (mid-x of crop)
    def fringe_count(im, box):
        c = crop(im, box)
        px = c.load()
        w, h = c.size
        x = w // 3  # through left stem of T
        bg = luma(px[0, 0])
        fringe = 0
        inside = 0
        for y in range(h):
            L = luma(px[x, y])
            d = abs(L - bg)
            if d > 8 and L < 220:
                fringe += 1
            elif L >= 220:
                inside += 1
        return fringe, inside, x

    for name, im in (("now", now_sizes), ("FT", ft_sizes), ("old", old_sizes)):
        print(f"96T stem fringe {name}:", fringe_count(im, crops["sizes_96_T"]))


if __name__ == "__main__":
    main()
