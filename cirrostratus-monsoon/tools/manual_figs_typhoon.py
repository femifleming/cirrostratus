"""Typhoon panel figures for the Typhoon manual (make_manual.py --module
typhoon), drawn with the Typhoon video's panel code
(tools/video/typhoon_video.py) the way manual_figs.py draws the Monsoon's:
the chapter's ground, highlighted controls, the LEDs in the state the
section talks about, and numbered badges that match the table rows.

Importing this switches build_video to the Typhoon for the whole process,
so the Monsoon's manual_figs must not be used in the same run."""
import os, re, sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "video"))
import build_video as bv  # noqa: E402

bv.use_module("typhoon")
import typhoon_video as tv  # noqa: E402

MARGIN_X, MARGIN_Y = 64, 44
PW = int(tv.PANEL_W * tv.PANEL_S)
PH = int(tv.PANEL_H * tv.PANEL_S)
FIG_W, FIG_H = PW + 2 * MARGIN_X, PH + 2 * MARGIN_Y
F_BADGE = bv.font("Inter-Medium.ttf", 21)
CIRCLED = "①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳"
T_FIG = 0.2  # blinking LEDs are in their "on" half at this time


def ground(i):
    return bv.hexrgb(bv.GROUNDS[i % len(bv.GROUNDS)])


def accent(i):
    return bv.hexrgb(bv.ACCENTS[i % len(bv.ACCENTS)])


def leds(*spec):
    """An LED state from a typhoon_video state spec, e.g. leds("mode", 5)."""
    return tv.state(spec, T_FIG)[0]


PANEL_LEDS = leds("meters")


def badge_keys(hl):
    """Controls in a highlight list that get a badge: not the CV jack of a
    highlighted slider or knob, nor the second half of a pair."""
    pairs = {"voct": "tune", "freq": "tune", "hold": "freeze", "in_r": "in_l", "out_r": "out_l",
             "in_vca": "in_level", "out_vca": "out_level"}
    out = []
    for k in hl:
        if re.match(r"(cv|kj)\d", k):
            continue
        if k in pairs and pairs[k] in hl:
            continue
        out.append(k)
    return out


def badge_pos(key):
    kind, x, y, r = tv.PANEL[key]
    if kind == "slider":
        return tv.P(x, tv.SL_TOP + 36)
    if kind == "box":
        x0, y0, x1, y1 = r
        if key == "meters":
            return tv.P((x0 + x1) / 2, y1 + 30)
        return tv.P((x0 + x1) / 2, y0 - 32)
    cx, cy = tv.P(x, y)
    rr = (r + 26) * tv.PANEL_S
    return cx + rr * 0.78, cy - rr * 0.78


def draw_badge(d, xy, text, fill, ink):
    w = max(30, F_BADGE.getlength(text) + 18)
    x, y = xy
    d.rounded_rectangle([x - w / 2, y - 15, x + w / 2, y + 15], radius=15, fill=fill)
    d.text((x - F_BADGE.getlength(text) / 2, y - 13), text, font=F_BADGE, fill=ink)


def render(path, badges=None, hl=None, leds=None, freeze_on=False, gi=0, t=0.55):
    badges = badges or {}
    hl = list(dict.fromkeys(list(hl or []) + list(badges)))
    g, acc = ground(gi), accent(gi)
    bv.PANEL_X, bv.PANEL_Y = MARGIN_X, MARGIN_Y
    img = Image.new("RGB", (FIG_W, FIG_H), g)
    d = ImageDraw.Draw(img)
    c = dict(ground=g, accent=acc)
    tv.panel_static(d, c)
    tv.panel_dynamic(d, c, t, hl, leds or tv.blank(), freeze_on)
    for k, label in badges.items():
        draw_badge(d, badge_pos(k), label, acc, bv.mix(g, (0, 0, 0), 0.3))
    img.save(path, optimize=True)


def number_table(rows):
    """rows: list of (first cell, second cell). Returns (labels, badges):
    a circled number per row that touches the panel, and the badge map."""
    labels, badges, n = [], {}, 0
    for a, b in rows:
        hl = tv.hl_for(bv.clean_md(a)) or tv.hl_for(bv.clean_md(b))
        keys = badge_keys(hl)
        if not keys:
            labels.append(None)
            continue
        mark = CIRCLED[n] if n < len(CIRCLED) else str(n + 1)
        n += 1
        labels.append(mark)
        for k in keys:
            badges[k] = badges[k] + " " + str(n) if k in badges else str(n)
    return labels, badges


def mode_leds(num):
    """The menu LEDs for a mode: its number on 1 2 4 8, A for bank 1, B for 2."""
    return leds("mode", num)


# Figures for the general chapters, keyed by the <!-- PANEL name --> markers
# in docs/TYPHOON_MANUAL.md. Badge labels follow the numbered steps.
GENERAL = {
    "cover": dict(leds=leds("mode", 14), freeze_on=True, gi=0),
    "quick": dict(badges={"time": "1", "tleds": "1", "sl0": "2", "bank": "2", "sl1": "4", "sl2": "4",
                          "sl3": "4", "k1": "4", "k2": "4", "k3": "4"},
                  leds=leds("mode", 1), gi=1),
    "install": dict(badges={"in_l": "2", "freeze": "3", "in_level": "4", "meters": "5", "tleds": "5",
                            "bleds": "5"},
                    leds=leds("install"), freeze_on=True, gi=3),
    "sunrise": dict(leds=tv.splash(1050)[0], freeze_on=True, gi=5),
    "look": dict(badges={"meters": "IN / OUT", "time": "tap", "bleds": "knobs", "freeze": "LED"},
                 leds=leds("knobs"), gi=2),
    "menu": dict(badges={"time": "1 hold", "tleds": "1", "bleds": "1", "sl0": "2", "bank": "3",
                         "freeze": "4"},
                 leds=leds("mode", 5), gi=4),
    "slots": dict(badges={"bank": "hold / tap", "time": "pick", "bleds": "slot", "freeze": "morph"},
                  leds=leds("load", 1, 3), gi=6),
    "envelope": dict(badges={"freeze": "hold", "time": "tap", "bleds": "up", "tleds": "down",
                             "k0": "1/A", "k1": "2/B", "k2": "4/C", "k3": "8/D"},
                     leds=leds("env"), freeze_on=True, gi=7),
    "monobass": dict(badges={"freeze": "hold", "bank": "tap", "tleds": "2 4", "bleds": "B C"},
                     leds=leds("monobass", 1), freeze_on=True, gi=8),
    "quality": dict(badges={"bank": "hold", "time": "tap", "freeze": "step", "tleds": "rate",
                            "bleds": "bits"},
                    leds=leds("bq", 1), gi=9),
    "fft": dict(badges={"time": "hold", "freeze": "tap / step", "bank": "apply", "tleds": "size"},
                leds=leds("fft", 2), gi=9),
    "calibrate": dict(badges={"trigb": "TRIG", "time": "or", "bank": "or", "meters": "blink"}, hl=["voct"],
                      leds=leds("cal", 2), gi=2),
    "mutes": dict(badges={"in_mute": "IN", "out_mute": "OUT", "meters": "red blinks"},
                  leds=leds("mute", 1, 0), gi=5),
    "aux": dict(badges={"aux": "AUX", "in_mute": "hold", "out_mute": "hold"},
                hl=["cv0", "cv1", "cv2", "cv3", "kj0", "kj1", "kj2", "kj3", "freq"],
                leds=leds("aux", 1), gi=1),
    "output": dict(badges={"k0": "WET", "out_l": "OUT", "out_mute": "mute", "out_level": "level"},
                   hl=["out_r", "out_vca"], leds=leds("meters"), gi=3),
}
