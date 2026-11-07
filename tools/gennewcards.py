#!/usr/bin/env python3
"""Cards for the shelves added 2026-11-07, matched to TortOS's classic deck.

Measured off cards/classic/NES.png: 640x820, rounded 28px, card stock a
vertical gradient (35,38,50) -> (25,28,38), a 12px accent rule at y=587, the
label in (239,239,244) in menu.ttf, letter-spaced. One flat silhouette in the
system's accent colour, no strokes - the deck has none.

Fancy deck: the same silhouette on transparent, trimmed, about 369x251, since
fancy is a hardware photograph with no card behind it and there are no
photographs here to use.

usage: gencards_new.py <font.ttf> <classic_out_dir> <fancy_out_dir>
"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H, R = 640, 820, 28
TOP, BOT = (35, 38, 50), (25, 28, 38)
LABEL = (239, 239, 244)
RULE_Y, RULE_H = 587, 12
SS = 4
STOCK = (30, 34, 45)  # knock-outs: roughly the stock at the mark's height


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


# Each mark draws into a SS-scaled ImageDraw, in card units (x 0..640, y 0..560)
def S(v):
    return [c * SS for c in v]


def rr(d, box, r, fill):
    d.rounded_rectangle(S(box), radius=r * SS, fill=fill)


def a2600(d, c, k):           # joystick: base, stick, button
    rr(d, (190, 300, 450, 440), 26, c)
    rr(d, (300, 140, 340, 330), 18, c)
    d.ellipse(S((270, 110, 370, 200)), fill=c)
    d.ellipse(S((205, 318, 255, 352)), fill=k)


def a7800(d, c, k):           # wide cartridge with a label well
    rr(d, (170, 140, 470, 420), 16, c)
    rr(d, (210, 180, 430, 330), 10, k)
    for i in range(6):
        d.rectangle(S((200 + i * 42, 395, 222 + i * 42, 420)), fill=k)


def lynx(d, c, k):            # landscape handheld
    rr(d, (110, 190, 530, 390), 60, c)
    rr(d, (235, 215, 405, 365), 10, k)
    d.rectangle(S((150, 275, 210, 305)), fill=k)
    d.rectangle(S((165, 260, 195, 320)), fill=k)
    d.ellipse(S((440, 250, 480, 290)), fill=k)
    d.ellipse(S((470, 295, 510, 335)), fill=k)


def ws(d, c, k):              # portrait handheld, screen high
    rr(d, (215, 110, 425, 450), 30, c)
    rr(d, (245, 140, 395, 280), 8, k)
    d.ellipse(S((250, 330, 300, 380)), fill=k)
    d.ellipse(S((340, 315, 380, 355)), fill=k)
    d.ellipse(S((360, 365, 400, 405)), fill=k)


def vb(d, c, k):              # visor with two lenses, on a stand
    rr(d, (130, 170, 510, 330), 70, c)
    d.ellipse(S((185, 205, 295, 295)), fill=k)
    d.ellipse(S((345, 205, 455, 295)), fill=k)
    d.rectangle(S((300, 330, 340, 420)), fill=c)
    rr(d, (230, 410, 410, 440), 12, c)


def pmini(d, c, k):           # small rounded handheld
    rr(d, (200, 130, 440, 440), 90, c)
    rr(d, (250, 180, 390, 290), 14, k)
    d.ellipse(S((295, 330, 345, 380)), fill=k)
    d.ellipse(S((240, 340, 275, 375)), fill=k)
    d.ellipse(S((365, 340, 400, 375)), fill=k)


def pico8(d, c, k):           # the digit 8 in fat pixels
    px = 36
    rows = ["01110", "10001", "10001", "01110", "10001", "10001", "01110"]
    x0, y0 = 320 - 5 * px / 2, 290 - 7 * px / 2
    for j, row in enumerate(rows):
        for i, b in enumerate(row):
            if b == "1":
                d.rectangle(S((x0 + i * px, y0 + j * px,
                               x0 + (i + 1) * px - 4, y0 + (j + 1) * px - 4)),
                            fill=c)


def mame(d, c, k):            # arcade cabinet in profile... from the front
    d.polygon(S((220, 110, 420, 110, 420, 460, 220, 460)), fill=c)
    rr(d, (240, 130, 400, 175), 6, k)          # marquee
    rr(d, (245, 195, 395, 315), 8, k)          # screen
    d.rectangle(S((220, 335, 420, 350)), fill=k)
    d.ellipse(S((262, 312, 292, 342)), fill=c)  # stick on the panel
    d.ellipse(S((335, 320, 355, 340)), fill=c)
    d.ellipse(S((365, 320, 385, 340)), fill=c)
    rr(d, (290, 390, 350, 420), 4, k)          # coin door


def neogeo(d, c, k):          # tall AES cartridge
    rr(d, (220, 110, 420, 450), 14, c)
    rr(d, (250, 150, 390, 330), 8, k)
    d.rectangle(S((240, 430, 400, 450)), fill=k)


SYSTEMS = [  # tag, label, accent, mark
    ("A2600", "ATARI 2600", "C00000", a2600),
    ("A7800", "ATARI 7800", "E05000", a7800),
    ("LYNX", "LYNX", "D9A400", lynx),
    ("WS", "WONDERSWAN", "4A6FA5", ws),
    ("VB", "VIRTUAL BOY", "C8102E", vb),
    ("PMINI", "POKÉMON MINI", "E3350D", pmini),
    ("PICO8", "PICO-8", "FF004D", pico8),
    ("MAME", "ARCADE", "B00000", mame),
    ("NEOGEO", "NEO GEO", "F0B400", neogeo),
]


def spaced(d, text, font, cx, y, track):
    widths = [d.textlength(ch, font=font) for ch in text]
    total = sum(widths) + track * (len(text) - 1)
    x = cx - total / 2
    for ch, w in zip(text, widths):
        d.text((x, y), ch, font=font, fill=LABEL)
        x += w + track


def mark_layer(fn, accent):
    lay = Image.new("RGBA", (W * SS, 560 * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    fn(d, accent + (255,), STOCK + (255,))
    return lay.resize((W, 560), Image.LANCZOS)


def card(font_path, tag, label, accent_hex, fn):
    accent = hexrgb(accent_hex)
    bg = Image.new("RGBA", (W, H))
    for y in range(H):
        t = y / (H - 1)
        col = tuple(round(TOP[i] + (BOT[i] - TOP[i]) * t) for i in range(3))
        ImageDraw.Draw(bg).line([(0, y), (W, y)], fill=col + (255,))
    bg.alpha_composite(mark_layer(fn, accent), (0, 0))
    d = ImageDraw.Draw(bg)
    d.rectangle((0, RULE_Y, W, RULE_Y + RULE_H - 1), fill=accent + (255,))
    font = ImageFont.truetype(font_path, 52)
    spaced(d, label, font, W / 2, 678, 10 if len(label) > 9 else 14)

    mask = Image.new("L", (W * SS, H * SS), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, W * SS - 1, H * SS - 1),
                                           radius=R * SS, fill=255)
    bg.putalpha(mask.resize((W, H), Image.LANCZOS))
    return bg


def fancy(fn, accent_hex):
    # On transparent, knock-outs transparent too: draw, then key the stock out
    lay = Image.new("RGBA", (W * SS, 560 * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    fn(d, hexrgb(accent_hex) + (255,), (0, 0, 0, 0))
    im = lay.resize((W, 560), Image.LANCZOS)
    im = im.crop(im.getbbox())
    im.thumbnail((369, 251), Image.LANCZOS)
    out = Image.new("RGBA", (369, 251), (0, 0, 0, 0))
    out.alpha_composite(im, ((369 - im.width) // 2, (251 - im.height) // 2))
    return out


def main():
    font, cdir, fdir = sys.argv[1:4]
    for tag, label, acc, fn in SYSTEMS:
        card(font, tag, label, acc, fn).save(f"{cdir}/{tag}.png", optimize=True)
        fancy(fn, acc).save(f"{fdir}/{tag}.png", optimize=True)
        print("wrote", tag)


if __name__ == "__main__":
    main()
