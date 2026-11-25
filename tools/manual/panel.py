"""Draws the Stages panel for the fletchfoorth manual.

Coordinates follow the photo of the real module (module at 437..912 x
72..932 in a 1360x1000 picture), scaled by S, so every control sits where it
does on the panel. A figure is a dict per column (0-5):
  led:     'green' | 'yellow' | 'red' | 'orange' | 'greenred' | 'yellowred' | None
  blink:   True draws the LED's blink marks
  slider:  0-1 cap position;  glow: 0-1 slider LED
  pot:     0-1 knob position
and figure-wide:
  rings:   [(col, part)]  accent rings (part: pot, led, button, slider, cv, gate, out)
  badges:  [(col, part, text)]
  dim:     columns drawn faded (not part of the story)
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONTS = os.path.expanduser("~/faltung-kit/assets/fonts")
S = 2.0
X0, Y0 = 437, 72
W, H = 475, 860
COLS = [485, 560, 636, 712, 789, 865]
Y_POT, Y_LED, Y_BTN = 186, 262, 305
Y_SL_TOP, Y_SL_BOT = 360, 540
Y_CV, Y_GATE, Y_OUT = 620, 715, 815
MARGIN = 70  # px around the module (for badges)

PANEL = (233, 233, 230)
EDGE = (190, 190, 186)
INK = (60, 62, 66)
BAND = (92, 98, 112)
ACCENT = (0, 132, 150)
ACCENT_INK = (255, 255, 255)
LED = {
    "green": (60, 220, 90), "yellow": (250, 200, 40), "red": (235, 50, 60),
    "orange": (250, 140, 40),
}


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), int(size))


F_LABEL = font("IBMPlexMono-Medium.ttf", 13 * S)
F_TITLE = font("Inter-Regular.ttf", 30 * S)
F_BADGE = font("Inter-Medium.ttf", 21 * S)
F_NUM = font("Inter-Medium.ttf", 14 * S)


def P(x, y):
    return MARGIN + (x - X0) * S, MARGIN + (y - Y0) * S


def circle(d, x, y, r, **kw):
    cx, cy = P(x, y)
    d.ellipse([cx - r * S, cy - r * S, cx + r * S, cy + r * S], **kw)


def text_c(d, x, y, s, f, fill):
    cx, cy = P(x, y)
    w = d.textlength(s, font=f)
    d.text((cx - w / 2, cy - f.size * 0.6), s, font=f, fill=fill)


def part_xy(col, part):
    x = COLS[col]
    return x, {"pot": Y_POT, "led": Y_LED, "button": Y_BTN, "slider": (Y_SL_TOP + Y_SL_BOT) / 2,
               "cv": Y_CV, "gate": Y_GATE, "out": Y_OUT}[part]


def part_r(part):
    return {"pot": 30, "led": 14, "button": 23, "cv": 30, "gate": 30, "out": 30}.get(part, 0)


def fade(c, amount):
    return tuple(int(v + (PANEL[i] - v) * amount) for i, v in enumerate(c))


def draw(path, cols=None, rings=(), badges=(), dim=(), title=True):
    cols = cols or [{} for _ in range(6)]
    img = Image.new("RGB", (int(W * S + 2 * MARGIN), int(H * S + 2 * MARGIN)), (255, 255, 255))
    d = ImageDraw.Draw(img)
    x0, y0 = P(X0, Y0)
    x1, y1 = P(X0 + W, Y0 + H)
    d.rectangle([x0, y0, x1, y1], fill=PANEL, outline=EDGE, width=int(2 * S))
    for hx, hy in [(487, 91), (861, 91), (487, 912), (861, 912)]:
        circle(d, hx, hy, 9, fill=(255, 255, 255), outline=EDGE)
    if title:
        text_c(d, 674, 100, "Stages", F_TITLE, INK)
    text_c(d, 674, 229, "SHAPE/TIME", F_LABEL, INK)
    text_c(d, 674, 572, "TIME/LEVEL", F_LABEL, INK)
    text_c(d, 674, 669, "GATE", F_LABEL, INK)
    bx0, by0 = P(450, 757)
    bx1, by1 = P(900, 856)
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=int(6 * S), fill=BAND)

    # accent rings go under the controls
    for col, part in rings:
        x, y = part_xy(col, part)
        if part == "slider":
            ax, ay = P(x - 20, Y_SL_TOP - 12)
            bx, by = P(x + 20, Y_SL_BOT + 12)
            d.rounded_rectangle([ax, ay, bx, by], radius=int(8 * S), outline=ACCENT, width=int(3 * S))
        else:
            circle(d, x, y, part_r(part) + 6, outline=ACCENT, width=int(3 * S))

    for i, x in enumerate(COLS):
        c = cols[i] if i < len(cols) else {}
        faded = i in dim
        f = (lambda col: fade(col, 0.65)) if faded else (lambda col: col)
        # pot
        circle(d, x, Y_POT, 22, fill=f((25, 25, 27)))
        import math
        a = math.radians(-135 + 270 * c.get("pot", 0.5))
        cx, cy = P(x, Y_POT)
        d.line([(cx + math.sin(a) * 6 * S, cy - math.cos(a) * 6 * S),
                (cx + math.sin(a) * 19 * S, cy - math.cos(a) * 19 * S)],
               fill=f((245, 245, 245)), width=int(3 * S))
        # type LED
        led = c.get("led")
        if led in ("greenred", "yellowred"):
            a_col, b_col = (LED["green"], LED["red"]) if led == "greenred" else (LED["yellow"], LED["red"])
            lx, ly = P(x, Y_LED)
            r = 8 * S
            d.pieslice([lx - r, ly - r, lx + r, ly + r], 90, 270, fill=f(a_col))
            d.pieslice([lx - r, ly - r, lx + r, ly + r], -90, 90, fill=f(b_col))
        elif led:
            circle(d, x, Y_LED, 8, fill=f(LED[led]))
            if not faded:
                circle(d, x, Y_LED, 12, outline=fade(LED[led], 0.5), width=int(2 * S))
        else:
            circle(d, x, Y_LED, 8, fill=f((215, 212, 200)))
        if c.get("blink") and led:
            lx, ly = P(x, Y_LED)
            for k in (-1, 1):
                d.line([(lx + k * 14 * S, ly - 8 * S), (lx + k * 19 * S, ly - 12 * S)], fill=INK, width=int(2 * S))
        # button
        circle(d, x, Y_BTN, 16, fill=f((25, 25, 27)))
        # slider slot and cap (with its LED)
        ax, ay = P(x - 8, Y_SL_TOP)
        bx, by = P(x + 8, Y_SL_BOT)
        d.rectangle([ax, ay, bx, by], fill=f((20, 20, 22)))
        d.line([P(x, Y_BTN + 18), P(x, Y_SL_TOP - 4)], fill=f((120, 120, 120)), width=int(1 * S))
        pos = c.get("slider", 0.15)
        cy_ = Y_SL_BOT - 22 - (Y_SL_BOT - Y_SL_TOP - 44) * pos
        ax, ay = P(x - 12, cy_ - 22)
        bx, by = P(x + 12, cy_ + 22)
        d.rectangle([ax, ay, bx, by], fill=f((35, 35, 38)), outline=f((90, 90, 95)), width=int(S))
        glow = c.get("glow", 0.0)
        gcol = tuple(int(40 + (v - 40) * glow) for v in LED["green"]) if glow > 0 else (70, 70, 70)
        ax, ay = P(x - 5, cy_ - 7)
        bx, by = P(x + 5, cy_ + 7)
        d.rectangle([ax, ay, bx, by], fill=f(gcol))
        # jacks
        for y, band in ((Y_CV, False), (Y_GATE, False), (Y_OUT, True)):
            circle(d, x, y, 22, fill=f((200, 200, 198)), outline=f((150, 150, 148)), width=int(2 * S))
            circle(d, x, y, 11, fill=f((15, 15, 15)))
        text_c(d, x + 8, 772, str(i + 1), F_NUM, (240, 240, 240) if not faded else (170, 170, 175))

    for col, part, label in badges:
        x, y = part_xy(col, part)
        r = part_r(part) + 4 if part != "slider" else 24
        if part == "slider":
            y = Y_SL_TOP - 10
        cx, cy = P(x, y)
        bxp, byp = cx + r * S * 0.75, cy - r * S * 0.75
        w = max(30 * S, d.textlength(label, font=F_BADGE) + 14 * S)
        h = 34 * S
        d.rounded_rectangle([bxp - w / 2, byp - h / 2, bxp + w / 2, byp + h / 2], radius=int(h / 2), fill=ACCENT)
        d.text((bxp - d.textlength(label, font=F_BADGE) / 2, byp - F_BADGE.size * 0.62), label,
               font=F_BADGE, fill=ACCENT_INK)
    img.save(path, optimize=True)


if __name__ == "__main__":
    import sys
    cols = [dict(led="green", glow=0.8, slider=0.6, pot=0.2), dict(led="green", glow=0.3, slider=0.4),
            dict(led="yellow", glow=0.5), dict(led="red", glow=1.0, slider=0.2, pot=0.7),
            dict(led="greenred", glow=1.0, slider=0.5), dict(led="yellowred", blink=True, glow=0.2)]
    draw(sys.argv[1], cols, rings=[(3, "slider"), (3, "pot"), (3, "gate")],
         badges=[(3, "slider", "attack"), (3, "pot", "decay"), (3, "gate", "gate")], dim=[5])
