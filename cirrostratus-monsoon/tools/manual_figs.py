"""Monsoon panel figures for the manual, drawn with the video manual's panel
code (tools/video/build_video.py) so the PDF shows the same module the video
does: deep-blue grounds, highlighted controls, the four LEDs in the state
the section talks about, and numbered badges that match the table rows."""
import os, re, sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "video"))
import build_video as bv  # noqa: E402

MARGIN_X, MARGIN_Y = 64, 44
PW = int(bv.PANEL_W * bv.PANEL_S)
PH = int(bv.PANEL_H * bv.PANEL_S)
FIG_W, FIG_H = PW + 2 * MARGIN_X, PH + 2 * MARGIN_Y
F_BADGE = bv.font("Inter-Medium.ttf", 21)
CIRCLED = "①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳"

AMBER = (1.0, 0.63)
GREEN = (0.0, 1.0)
RED = (1.0, 0.0)
OFF = (0.0, 0.0)


def ground(i):
    return bv.hexrgb(bv.GROUNDS[i % len(bv.GROUNDS)])


def accent(i):
    return bv.hexrgb(bv.ACCENTS[i % len(bv.ACCENTS)])


def badge_keys(hl):
    """Controls in a highlight list that get a badge: skip the CV jack that
    belongs to a highlighted knob/slider/button."""
    out = []
    for k in hl:
        if re.match(r"(cv|kj)\d", k):
            continue
        if (k == "voct" and "tune" in hl) or (k == "fgate" and "freeze" in hl) or \
           (k == "out_r" and "out_l" in hl):
            continue
        out.append(k)
    return out


def badge_pos(key):
    kind, x, y, r = bv.PANEL[key]
    if kind == "slider":
        return bv.P(x, bv.SL_TOP - 62)
    if kind == "leds":
        return bv.P(x, y - 92)
    cx, cy = bv.P(x, y)
    rr = (r + 26) * bv.PANEL_S
    return cx + rr * 0.78, cy - rr * 0.78


def draw_badge(d, xy, text, fill, ink):
    w = max(30, F_BADGE.getlength(text) + 18)
    x, y = xy
    d.rounded_rectangle([x - w / 2, y - 15, x + w / 2, y + 15], radius=15, fill=fill)
    d.text((x - F_BADGE.getlength(text) / 2, y - 13), text, font=F_BADGE, fill=ink)


def render(path, badges=None, hl=None, leds=None, freeze_on=False, gi=0, t=0.55):
    """badges: {panel key: label}. hl: extra keys to highlight (badged keys
    are always highlighted)."""
    badges = badges or {}
    hl = list(dict.fromkeys(list(hl or []) + list(badges)))
    g, acc = ground(gi), accent(gi)
    bv.PANEL_X, bv.PANEL_Y = MARGIN_X, MARGIN_Y
    img = Image.new("RGB", (FIG_W, FIG_H), g)
    d = ImageDraw.Draw(img)
    c = dict(ground=g, accent=acc)
    bv.panel_static(d, c)
    bv.panel_dynamic(d, c, t, hl, leds or [OFF] * 4, freeze_on)
    for k, label in badges.items():
        draw_badge(d, badge_pos(k), label, acc, bv.mix(g, (0, 0, 0), 0.3))
    img.save(path, optimize=True)


def number_table(rows):
    """rows: list of (first cell, second cell). Returns (labels, badges):
    a circled number per row that touches the panel, and the badge map."""
    labels, badges, n = [], {}, 0
    for a, b in rows:
        hl = bv.hl_for(bv.clean_md(a)) or bv.hl_for(bv.clean_md(b))
        keys = badge_keys(hl)
        if not keys:
            labels.append(None)
            continue
        mark = CIRCLED[n] if n < len(CIRCLED) else str(n + 1)
        n += 1
        labels.append(mark)
        for k in keys:  # the drawing's font has no circled digits: plain ones
            badges[k] = badges[k] + " " + str(n) if k in badges else str(n)
    return labels, badges


def mode_leds(num):
    """The menu LEDs for a mode: its number in binary, green bank 1, red bank 2."""
    green = num <= 15
    return bv.binary_leds(num if green else num - 15, green)


# Figures for the general chapters, keyed by the <!-- PANEL name --> markers
# in docs/MANUAL.md. Badge labels follow the numbered steps in the text.
GENERAL = {
    "cover": dict(leds=[AMBER, GREEN, GREEN, AMBER], freeze_on=True, gi=0),
    "quick": dict(badges={"disp": "1", "sl0": "2", "write": "3", "sl1": "4", "sl2": "4", "sl3": "4",
                          "k1": "4", "k2": "4", "k3": "4"},
                  leds=mode_leds(1), gi=1),
    "install": dict(badges={"in_l": "2", "freeze": "3", "gain": "4", "leds": "5"},
                    leds=[GREEN] * 4, freeze_on=True, gi=3),
    "sunrise": dict(leds=[AMBER, (0.4, 0.8), (0.2, 0.9), GREEN], freeze_on=True, gi=5),
    "look": dict(badges={"disp": "tap", "freeze": "LED"},
                 leds=[GREEN, GREEN, (0.7, 0.8), OFF], gi=2),
    "menu": dict(badges={"disp": "1 hold", "sl0": "2", "write": "3", "freeze": "4"},
                 leds=mode_leds(5), gi=4),
    "slots": dict(badges={"write": "hold / tap", "disp": "pick", "freeze": "morph"},
                  leds=[OFF, RED, OFF, OFF], gi=6),
    "envelope": dict(badges={"freeze": "hold", "disp": "tap", "k0": "WET", "k1": "BAL", "k2": "FBK",
                             "k3": "RVB"},
                     leds=[(0.0, 0.45), GREEN, OFF, RED], gi=7),
    "monobass": dict(badges={"freeze": "hold", "write": "tap"},
                     leds=[OFF, GREEN, GREEN, OFF], gi=8),
    "quality": dict(badges={"write": "hold", "disp": "tap", "freeze": "step"},
                    leds=bv.bq_leds(1, pending=True), gi=9),
    "fft": dict(badges={"disp": "hold", "freeze": "tap / step", "write": "apply"},
                leds=[AMBER, AMBER, OFF, OFF], gi=9),
    "calibrate": dict(badges={"disp": "1", "write": "1 2 3", "voct": "2 3"},
                      leds=[GREEN, GREEN, OFF, OFF], gi=2),
    "output": dict(badges={"k0": "WET", "out_l": "OUT"}, hl=["out_r", "freeze", "write"],
                   leds=[GREEN, GREEN, GREEN, OFF], gi=3),
}
