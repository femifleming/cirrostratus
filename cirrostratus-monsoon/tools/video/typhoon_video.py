"""The Typhoon edition of the video manual (build_video.py --module typhoon)
and of the manual's panel figures (tools/manual_figs_typhoon.py).

install(bv) swaps build_video's Monsoon panel, LEDs, chapters and wording
for the Typhoon's, so the frame layout, narration, music and rendering stay
the Monsoon video's own code. The Typhoon's behaviour comes from
docs/TYPHOON.md; the modes are the same 30, read from docs/modes_*.md and
renamed by tools/typhoon_text.py (DISP = TIME, WRITE = BANK, BAL / FBK / RVB
= STEREO / FEEDBACK / REVERB).

The panel is drawn flat, like the Monsoon's, from a photo of the module
(1042 x 1630, x right, y down): silver face, black type, single-colour LEDs.
"""
import math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))  # tools/, for typhoon_text
import typhoon_text  # noqa: E402

bv = None  # build_video, set by install()

# ---------------------------------------------------------------------------
# Panel geometry, in photo units.

PANEL_W, PANEL_H = 1042, 1630
PANEL_X, PANEL_Y = 1026, 104
PANEL_S = 0.515

SL_X = [295, 450, 600, 750]
SL_TOP, SL_BOT = 295, 775
KN_X = [250, 435, 610, 800]
KJ_X = [260, 435, 610, 790]
KN_Y = 940
LED_X = [412, 487, 557, 630]          # meter rows, green green yellow red
LED_ROW_Y = {"in": 147, "out": 205}
COL_Y = [768, 830, 895, 960]          # A B C D (left), 1 2 4 8 (right)
BANK_COL_X, TIME_COL_X = 70, 970
JR1_Y, JR2_Y = 1275, 1425

PANEL = {
    "tune": ("knob", 95, 170, 70),
    "in_mute": ("button", 310, 165, 25),
    "out_mute": ("button", 735, 165, 25),
    "freeze": ("fbutton", 930, 190, 50),
    "in_level": ("knob", 100, 530, 62),
    "out_level": ("knob", 950, 530, 62),
    "bank": ("button", 75, 1055, 30),
    "time": ("button", 970, 1055, 30),
    "meters": ("box", 521, 176, (374, 112, 668, 240)),
    "bleds": ("box", 84, 864, (34, 730, 146, 998)),
    "tleds": ("box", 956, 864, (894, 730, 1006, 998)),
    "in_vca": ("jack", 70, JR1_Y, 36), "aux": ("jack", 190, JR1_Y, 36),
    "trig": ("jack", 840, JR1_Y, 36), "out_vca": ("jack", 970, JR1_Y, 36),
    "in_l": ("jack", 70, JR2_Y, 36), "in_r": ("jack", 190, JR2_Y, 36),
    "trigb": ("button", 330, JR2_Y, 28), "voct": ("jack", 455, JR2_Y, 36),
    "freq": ("jack", 585, JR2_Y, 36), "hold": ("jack", 715, JR2_Y, 36),
    "out_l": ("jack", 840, JR2_Y, 36), "out_r": ("jack", 970, JR2_Y, 36),
}
for _i in range(4):
    PANEL["sl%d" % _i] = ("slider", SL_X[_i], (SL_TOP + SL_BOT) // 2, 30)
    PANEL["cv%d" % _i] = ("jack", [330, 455, 585, 715][_i], JR1_Y, 36)
    PANEL["k%d" % _i] = ("sknob", KN_X[_i], KN_Y, 60)
    PANEL["kj%d" % _i] = ("jack", KJ_X[_i], 1130, 36)

KNOB_ANGLE = {"tune": 0, "in_level": 0, "out_level": 0, "k0": -140, "k1": 0, "k2": -150, "k3": -150}

GREEN, YELLOW, RED = (70, 225, 95), (255, 206, 46), (245, 64, 48)
METER_COLS = [GREEN, GREEN, YELLOW, RED]

F_LAB = None
F_LAB_S = None


def P(x, y):
    return bv.PANEL_X + x * bv.PANEL_S, bv.PANEL_Y + y * bv.PANEL_S


def circ(d, x, y, r, **kw):
    cx, cy = P(x, y)
    rr = r * bv.PANEL_S
    d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], **kw)


def ptext(d, x, y, s, fill, f=None):
    f = f or F_LAB
    cx, cy = P(x, y)
    d.text((cx - f.getlength(s) / 2, cy - f.size * 0.6), s, font=f, fill=fill)


def colours(c):
    mix, INK = bv.mix, bv.INK
    g = c["ground"]
    face = mix(g, INK, 0.78)                 # aluminium
    return dict(g=g, face=face, edge=mix(g, INK, 0.5), lab=mix(g, (0, 0, 0), 0.25),
                hole=mix(g, (0, 0, 0), 0.55), body=mix(g, (0, 0, 0), 0.3),
                ring=mix(face, (0, 0, 0), 0.42), off=mix(face, (0, 0, 0), 0.62),
                hl=mix(c["accent"], g, 0.38))


def panel_static(d, c):
    k = colours(c)
    mix = bv.mix
    face, lab, hole = k["face"], k["lab"], k["hole"]
    x0, y0 = P(0, 0)
    x1, y1 = P(PANEL_W, PANEL_H)
    d.polygon([(x0 + 18, y0 + 18), (x1 + 18, y0 + 18), (x1 + 18, y1 + 18), (x0 + 18, y1 + 18)],
              fill=mix(k["g"], (0, 0, 0), 0.3))
    d.rectangle([x0, y0, x1, y1], fill=face, outline=k["edge"], width=2)
    for sx, sy in [(60, 30), (982, 30), (60, 1600), (982, 1600)]:
        circ(d, sx, sy, 15, fill=mix(face, (0, 0, 0), 0.5))
    ptext(d, PANEL_W / 2, 66, "TYPHOON", lab, bv.F_PANEL_T)
    # top row
    ptext(d, 95, 268, "TUNE", lab)
    ptext(d, 310, 245, "IN MUTE", lab, F_LAB_S)
    ptext(d, 735, 245, "OUT MUTE", lab, F_LAB_S)
    ptext(d, 930, 290, "FREEZE", lab)
    ptext(d, 100, 640, "IN LEVEL", lab, F_LAB_S)
    ptext(d, 950, 640, "OUT LEVEL", lab, F_LAB_S)
    for key in ("tune", "in_level", "out_level"):
        _, x, y, r = PANEL[key]
        circ(d, x, y, r, fill=k["body"])
    # sliders: slots, labels, the short lines between them
    for i, x in enumerate(SL_X):
        ax, ay = P(x - 15, SL_TOP)
        bx, by = P(x + 15, SL_BOT)
        d.rounded_rectangle([ax, ay, bx, by], radius=6, fill=hole)
        ptext(d, x, 810, ["POSITION", "DENSITY", "SIZE", "TEXTURE"][i], lab, F_LAB_S)
    for i in range(3):
        a, yy = P(SL_X[i] + 38, 540)
        b, _ = P(SL_X[i + 1] - 38, 540)
        d.line([(a, yy), (b, yy)], fill=mix(face, (0, 0, 0), 0.4), width=1)
    # LED columns and their labels
    for i, y in enumerate(COL_Y):
        ptext(d, 118, y, "ABCD"[i], lab)
        ptext(d, 925, y, "1248"[i], lab)
    ptext(d, 75, 1122, "BANK", lab)
    ptext(d, 970, 1122, "TIME", lab)
    # the four small knobs and their labels
    for i, x in enumerate(KN_X):
        circ(d, x, KN_Y, PANEL["k%d" % i][3], fill=k["body"])
        ptext(d, x, 1033, ["WET", "STEREO", "FEEDBACK", "REVERB"][i], lab, F_LAB_S)
    # jacks
    for key, (kind, x, y, r) in PANEL.items():
        if kind == "jack":
            circ(d, x, y, r + 6, outline=k["ring"], width=2)
            circ(d, x, y, r * 0.55, fill=hole)
    for x, s in zip([70, 190, 330, 455, 585, 715, 840, 970],
                    ["IN VCA", "AUX", "POSI", "DENS", "SIZE", "TEXT", "TRIG", "OUT VCA"]):
        ptext(d, x, 1345, s, lab, F_LAB_S)
    for x, s in zip([70, 190, 330, 455, 585, 715, 840, 970],
                    ["IN L", "IN R", "TRIG", "V/OCT", "FREQ", "HOLD", "OUT L", "OUT R"]):
        ptext(d, x, 1500, s, lab, F_LAB_S)
    for key in ("in_mute", "out_mute", "bank", "time", "trigb"):
        _, x, y, r = PANEL[key]
        circ(d, x, y, r + 5, fill=mix(face, (0, 0, 0), 0.35))
        circ(d, x, y, r, fill=k["body"])
    # the maker's mark: a little bear and the name
    bx, by = 420, 1584
    circ(d, bx, by, 19, fill=lab)
    circ(d, bx - 15, by - 16, 8, fill=lab)
    circ(d, bx + 15, by - 16, 8, fill=lab)
    circ(d, bx - 7, by - 3, 3, fill=face)
    circ(d, bx + 7, by - 3, 3, fill=face)
    ptext(d, 545, 1582, "calsynth", lab, F_LAB)


def led(d, x, y, r, col, v, k):
    """A single-colour LED at brightness v (0..1)."""
    v = max(0.0, min(1.0, v))
    if v < 0.02:
        circ(d, x, y, r, fill=k["off"])
        return
    on = bv.mix(k["off"], col, 0.35 + 0.65 * v)
    if v > 0.2:
        circ(d, x, y, r + 9, fill=bv.mix(k["face"], col, 0.35 * v))
    circ(d, x, y, r, fill=on)


def panel_dynamic(d, c, t, hl, leds, freeze_on):
    k = colours(c)
    mix, INK = bv.mix, bv.INK
    acc, g, hc = c["accent"], k["g"], k["hl"]
    leds = leds if isinstance(leds, dict) else blank()
    pulse = 0.5 + 0.5 * math.sin(t * 5.0)
    for key in hl:
        kind, x, y, r = PANEL[key]
        if kind == "slider":
            ax, ay = P(x - 30, SL_TOP - 14)
            bx, by = P(x + 30, SL_BOT + 14)
            d.rounded_rectangle([ax, ay, bx, by], radius=10, outline=hc, width=3)
        elif kind == "box":
            ax, ay = P(r[0], r[1])
            bx, by = P(r[2], r[3])
            d.rounded_rectangle([ax, ay, bx, by], radius=12, outline=hc, width=3)
        else:
            circ(d, x, y, r + 18 + 8 * pulse, outline=hc, width=3)
    for i, x in enumerate(SL_X):
        on = ("sl%d" % i) in hl
        v = 0.5 + (0.38 * math.sin(t * 1.4 + i) if on else 0.0)
        cy = SL_BOT - (SL_BOT - SL_TOP) * v
        ax, ay = P(x - 26, cy - 30)
        bx, by = P(x + 26, cy + 30)
        lean = (by - ay) * 0.25
        col = hc if on else mix(k["face"], (0, 0, 0), 0.6)
        d.polygon([(ax + lean, ay), (bx + lean, ay), (bx, by), (ax, by)], fill=col)
        _, my = P(x, cy)
        d.line([(ax + 2, my), (bx - 2, my)], fill=INK, width=2)
    for key, base in KNOB_ANGLE.items():
        kind, x, y, r = PANEL[key]
        on = key in hl
        if on:
            circ(d, x, y, r, fill=mix(acc, g, 0.35))
        ang = math.radians(base + (70 * math.sin(t * 1.6) if on else 0))
        cx, cy = P(x, y)
        rr = r * bv.PANEL_S
        d.line([(cx + math.sin(ang) * rr * 0.35, cy - math.cos(ang) * rr * 0.35),
                (cx + math.sin(ang) * rr * 0.9, cy - math.cos(ang) * rr * 0.9)], fill=INK, width=3)
    for key in hl:
        kind, x, y, r = PANEL[key]
        if kind == "jack":
            circ(d, x, y, r + 6, outline=hc, width=3)
        elif kind == "button":
            circ(d, x, y, r, fill=mix(acc, g, 0.3))
    # FREEZE: a big white illuminated button
    _, fx, fy, fr = PANEL["freeze"]
    if freeze_on:
        circ(d, fx, fy, fr + 12, fill=mix(k["face"], (255, 255, 250), 0.6))
        circ(d, fx, fy, fr, fill=(253, 253, 249), outline=mix(k["face"], (0, 0, 0), 0.25), width=2)
    else:
        circ(d, fx, fy, fr, fill=mix(k["face"], (255, 255, 255), 0.45),
             outline=mix(k["face"], (0, 0, 0), 0.35), width=2)
    # 16 LEDs
    for row in ("in", "out"):
        for i, v in enumerate(leds[row]):
            led(d, LED_X[i], LED_ROW_Y[row], 17, METER_COLS[i], v, k)
    for i in range(4):
        led(d, BANK_COL_X, COL_Y[i], 16, YELLOW, leds["bank"][i], k)
        led(d, TIME_COL_X, COL_Y[i], 16, YELLOW, leds["time"][i], k)


def draw_connector(d, c, active, t):
    r = c["rows"][active]
    if not r["hl"]:
        return
    y0, gap = bv.row_geometry(c)
    y = y0 + active * gap + 15
    kind, x, py, rad = PANEL[r["hl"][0]]
    tx, ty = P(x, py)
    if kind == "slider":
        ty = P(x, SL_TOP - 14)[1] if y < ty else P(x, SL_BOT + 14)[1]
    elif kind == "box":
        tx, ty = P(rad[0], (rad[1] + rad[3]) / 2)
    else:
        tx -= (rad + 18) * bv.PANEL_S
    acc = c["accent"]
    elbow = 1006
    d.line([(bv.TEXT_R + 12, y), (elbow, y), (tx, ty)], fill=acc, width=2)
    d.ellipse([bv.TEXT_R + 6, y - 6, bv.TEXT_R + 18, y + 6], fill=acc)


def draw_rows(d, c, t, active):
    """build_video.draw_rows, except that a row text measured in the small
    font is also drawn in it (the Monsoon's draws it large, which can run
    into the panel; the Typhoon's panel is wider and starts further left)."""
    rows = c["rows"]
    if not rows:
        return
    mix, INK = bv.mix, bv.INK
    y0, gap = bv.row_geometry(c)
    dim = mix(INK, c["ground"], 0.45)
    small = gap < 44
    for i, r in enumerate(rows):
        y = y0 + i * gap
        on = i == active
        col = INK if on else dim
        d.text((bv.ROW_LX, y), r["label"].lower()[:19], font=bv.F_MONO_M if on else bv.F_MONO, fill=col)
        d.line([(bv.ROW_SX0, y + 15), (bv.ROW_SX1, y + 15)], fill=mix(INK, c["ground"], 0.65), width=1)
        v = 0.5 + 0.42 * math.sin(t * (0.6 + 0.13 * i) + i * 1.3)
        if on:
            v = 0.5 + 0.48 * math.sin(t * 1.7 + i)
        wv = (bv.ROW_SX1 - bv.ROW_SX0) * v
        bv.skew_rect(d, bv.ROW_SX0, y + 6, wv, 18, c["accent"] if on else mix(c["accent"], c["ground"], 0.45))
        d.line([(bv.ROW_SX0 + wv + 9, y + 4), (bv.ROW_SX0 + wv, y + 25)], fill=INK, width=2)
        text, width = r["text"].lower(), bv.TEXT_R - bv.ROW_TX
        if bv.F_MONO.getlength(text) <= width:
            d.text((bv.ROW_TX, y + 1), text, font=bv.F_MONO, fill=col)
            continue
        lines = bv.wrap(text, bv.F_MONO_S, width)
        if len(lines) == 1 or small:
            s = lines[0] + (" …" if len(lines) > 1 else "")
            d.text((bv.ROW_TX, y + 3), s, font=bv.F_MONO_S, fill=col)
        else:
            d.text((bv.ROW_TX, y - 6), lines[0], font=bv.F_MONO_S, fill=col)
            d.text((bv.ROW_TX, y + 17), (lines[1] + (" …" if len(lines) > 2 else "")), font=bv.F_MONO_S,
                   fill=col)


# ---------------------------------------------------------------------------
# What a row or table label points at on the Typhoon.

SLIDERS = {"position": 0, "density": 1, "size": 2, "texture": 3}
KNOBS = {"wet": 0, "blend": 0, "bal": 1, "stereo": 1, "feedback": 2, "fbk": 2, "reverb": 3, "rvb": 3}


def hl_for(label):
    l = label.lower()
    keys = []
    for word, i in SLIDERS.items():
        if word in l:
            keys += ["sl%d" % i, "cv%d" % i]
    for word, i in KNOBS.items():
        if re.search(r"\b%s\b" % word, l):
            keys += ["k%d" % i, "kj%d" % i]
    if "pitch" in l or "tune" in l:
        keys += ["tune", "voct", "freq"]
    if re.search(r"\bin level\b|\bgain\b", l):
        keys += ["in_level"]
    if re.search(r"\bout level\b", l):
        keys += ["out_level"]
    if re.search(r"\bin vca\b", l):
        keys += ["in_vca"]
    if re.search(r"\bout vca\b", l):
        keys += ["out_vca"]
    if re.search(r"\bin mute\b", l):
        keys += ["in_mute"]
    if re.search(r"\bout mute\b", l):
        keys += ["out_mute"]
    if "meter" in l:
        keys += ["meters"]
    if "freeze" in l:
        keys += ["freeze", "hold"]
    if "trig" in l:
        keys += ["trig", "trigb"]
    if re.search(r"\btime\b|\bdisp\b", l):
        keys += ["time"]
    if re.search(r"\bbank\b|\bwrite\b", l):
        keys += ["bank"]
    if "option" in l:
        keys += ["time", "freeze"]
    if re.search(r"\baux\b", l):
        keys += ["aux"]
    if re.search(r"\bin l\b", l):
        keys += ["in_l", "in_r"]
    if re.search(r"\bout l\b", l) or "limiter" in l:
        keys += ["out_l", "out_r"]
    return list(dict.fromkeys(keys))


# ---------------------------------------------------------------------------
# LED states: {"time", "bank", "in", "out": four levels 0..1 each}. Index 0
# is TIME "1" / BANK "A" (top of the columns) and the left (green) end of a
# meter row; index 3 is TIME "8" / BANK "D" / the red end.

def blank():
    return {"time": [0.0] * 4, "bank": [0.0] * 4, "in": [0.0] * 4, "out": [0.0] * 4}


def binary(n, level=1.0):
    return [level if (n >> i) & 1 else 0.0 for i in range(4)]


def bar(v):
    """A meter row at level v (0..1): each LED takes a quarter, fading in."""
    return [max(0.0, min(1.0, v * 4 - i)) for i in range(4)]


def with_meters(st, t):
    st["in"] = bar(0.5 + 0.28 * math.sin(t * 2.3) + 0.12 * math.sin(t * 7.3 + 1))
    st["out"] = bar(0.45 + 0.25 * math.sin(t * 1.9 + 1.2) + 0.1 * math.sin(t * 6.1))
    return st


def show_mode(st, num, level=1.0):
    st["time"] = binary((num - 1) % 15 + 1, level)
    st["bank"][0 if num <= 15 else 1] = level
    return st


def splash(ms):
    """Ui::PaintSplash (typhoon/ui.cc), mode 14 as the forecast."""
    t = int(ms)
    st = blank()
    groups = ["time", "bank", "in", "out"]

    def tri(tt, period):
        if tt < 0 or tt >= period:
            return 0
        h = period // 2
        return tt * 255 // h if tt < h else (period - tt) * 255 // h
    if t < 800:
        for gi, g in enumerate(groups):
            for i in range(4):
                h = ((t // 90 + (gi * 4 + i) * 7919) * 2654435761) & 0xffffffff
                lvl = ((h >> 16) & 0x7f) if (h >> 24) > 170 else 0
                st[g][i] = lvl * min(t, 400) // 400 / 255
        return st, False
    if t < 1900:
        u, glow = t - 800, 0
        for pulse in range(2):
            p = u - pulse * 500
            inner, outer = tri(p, 400), tri(p - 150, 400)
            if inner or outer:
                for g in groups:
                    st[g] = [outer / 255, inner / 255, inner / 255, outer / 255]
            glow = max(glow, inner)
        return st, glow > 128
    if t < 2500:
        u = t - 1900
        for i in range(4):
            kk = max(0, min(255, (u - i * 90) * 255 // 300))
            for g in groups:
                st[g][i] = (96 + kk * 159 // 255) / 255
        return st, False
    u = t - 2500
    lvl = 1.0 if u < 500 else max(0.0, 1 - (u - 500) / 300)
    return show_mode(st, 14, lvl), False


def state(spec, t, c=None):
    """LEDs + FREEZE LED for a state spec: a tuple (name, args...), so that
    chapters stay picklable for the render workers."""
    name, args = spec[0], spec[1:]
    blink = int(t * 1000) & 255 > 127          # the firmware's blink rates
    fast = int(t * 1000) & 127 > 63
    st = blank()
    frz = False
    if name == "sunrise":
        return splash((t * 1000) % 4200)
    if name == "meters":                       # meters page, the mode dim
        return show_mode(with_meters(st, t), args[0] if args else 2, 40 / 255), False
    if name == "knobs":                        # knobs page
        with_meters(st, t)
        st["time"] = binary(2, 40 / 255)
        st["bank"] = [0.8, 0.5 + 0.3 * math.sin(t * 1.1), 0.35, 0.15 + 0.1 * math.sin(t * 0.7)]
        return st, False
    if name == "mode":
        return show_mode(with_meters(st, t), args[0]), False
    if name == "menu":                         # browse through the modes
        n = int(t / 1.2) % 30 + 1
        return show_mode(with_meters(st, t), n), False
    if name == "option":                       # option n blinks on TIME
        with_meters(st, t)
        st["bank"][0] = 1.0
        st["time"] = binary(args[0], 1.0 if blink else 0.0)
        return st, False
    if name == "install":                      # receiving: FREEZE fast, meters
        with_meters(st, t)
        if int(t * 2) % 4 == 3:                # a block being written
            st["time"] = [1.0] * 4
            st["bank"] = [1.0] * 4
        return st, int(t * 8) % 2 == 0
    if name == "install_err":
        on = 1.0 if blink else 0.0
        st["time"] = [on] * 4
        st["bank"] = [on] * 4
        return st, False
    if name == "power":                        # held FREEZE at power-up
        return st, int(t * 4) % 2 == 0
    if name == "load":                         # slot args[0] blinks, morph args[1] lit
        slot, morph = args[0], (args[1] if len(args) > 1 else -1)
        with_meters(st, t)
        if morph >= 0:
            st["bank"][morph] = 1.0
        st["bank"][slot] = 1.0 if blink else (40 / 255 if morph == slot else 0.0)
        return st, False
    if name == "load_cycle":                   # TIME stepping through the slots
        with_meters(st, t)
        st["bank"][int(t / 1.1) % 4] = 1.0 if blink else 0.0
        return st, False
    if name == "save":
        with_meters(st, t)
        st["bank"] = [24 / 255] * 4
        st["bank"][args[0]] = 1.0 if blink else 0.0
        return st, False
    if name == "env":                          # WET +1, STEREO off, FEEDBACK +1/2, REVERB -1
        with_meters(st, t)
        st["bank"] = [1.0, 0.0, 70 / 255, 0.0]
        st["time"] = [0.0, 0.0, 0.0, 1.0]
        cur = args[0] if args else -1
        if cur >= 0 and fast:
            e = [2, 0, 1, -2][cur]
            st["bank"][cur] = 40 / 255 if e == 0 else 0.0
            st["time"][cur] = 40 / 255 if e == 0 else 0.0
        return st, True
    if name == "monobass":
        with_meters(st, t)
        idx = (1, 2) if args[0] else (0, 3)
        for i in idx:
            st["time"][i] = st["bank"][i] = 1.0
        return st, True
    if name == "aux":
        with_meters(st, t)
        if args[0]:
            st["time"] = [1.0] * 4
            st["bank"] = [1.0] * 4
        else:
            st["time"][3] = st["bank"][3] = 1.0
        return st, False
    if name == "mute":                         # (in muted, out muted)
        with_meters(st, t)
        if args[0]:
            st["in"] = [0, 0, 0, 1.0 if blink else 0.0]
        if args[1]:
            st["out"] = [0, 0, 0, 1.0 if blink else 0.0]
        show_mode(st, 2, 40 / 255)
        return st, False
    if name in ("bq", "bq_cycle", "bq_sweep", "bq_applied"):
        with_meters(st, t)
        if name == "bq_sweep":
            k = int(t / 0.08) % 8
            if k < 4:
                st["time"][k] = 1.0
                return st, False
            q, pending = 0, False
        elif name == "bq_cycle":
            q, pending = int(t / 0.9) % 6, True
        else:
            q, pending = args[0], (args[1] if len(args) > 1 else False)
        lit, eight = 1 + q % 3, q >= 3
        if name == "bq_applied":
            if fast:
                st["time"] = [1.0] * 4
                st["bank"][1 if eight else 0] = 1.0
            return st, False
        for i in range(lit):
            st["time"][i] = 1.0
        st["bank"][1 if eight else 0] = 1.0
        st["time"][3] = 1.0 if (not pending or blink) else 0.0
        return st, False
    if name in ("fft", "fft_cycle", "fft_sweep", "fft_applied"):
        with_meters(st, t)
        if name == "fft_sweep":
            k = int(t / 0.08) % 8
            if k < 4:
                st["time"][3 - k] = 1.0
                return st, False
            s, pending = 0, False
        elif name == "fft_cycle":
            s, pending = 1 + int(t / 0.9) % 4, True
        else:
            s, pending = args[0], (args[1] if len(args) > 1 else False)
        own = s == 0
        lit = 4 if own else s                  # own: SPECTRAL's 2048
        if name == "fft_applied":
            if fast:
                st["time"] = [1.0] * 4
                st["bank"][0] = 1.0
                if not own:
                    st["bank"][1] = 1.0
            return st, False
        if not pending or blink:
            for i in range(lit):
                st["time"][i] = 1.0
            st["bank"][0] = 1.0
            if not own:
                st["bank"][1] = 1.0
        return st, False
    if name == "cal":
        on = 1.0 if blink else 0.0
        st["in"] = [on] * 4
        if args[0] == 2:
            st["out"] = [on] * 4
        return st, False
    if name == "panic":
        return {g: [1.0] * 4 for g in ("time", "bank", "in", "out")}, False
    raise ValueError(spec)


def chapter_leds(c, t, active):
    r = c["rows"][active] if active is not None else None
    spec = (r or {}).get("leds") or c.get("leds")
    if c["kind"] == "mode" and not (r and r.get("leds")):
        st, frz = state(("mode", c["num"]), t)
        if r and r["label"].lower().startswith("freeze"):
            frz = True
        return st, frz
    if not spec:
        spec = ("sunrise",) if c["kind"] == "sunrise" else ("meters",)
    return state(spec, t, c)


# ---------------------------------------------------------------------------
# Chapters.

def general_chapters():
    row = bv.row

    def R(label, text, spoken=None, hl=None, leds=None):
        r = row(label, text, spoken, hl)
        if leds:
            r["leds"] = leds
        return r
    return [
        dict(code="cs00", title="cirrostratus", sub="a thin, high sheet of cloud for the typhoon",
             desc="Alternative firmware for the Typhoon, Calsynth's Clouds on the Supercell layout and the "
                  "Monsoon's bigger sibling. Thirty modes in two banks, a forty-eight kilohertz "
                  "floating-point engine, and eight hands-on controls in every mode.",
             say="Cirrostratus. A thin, high sheet of cloud, now for the Typhoon: the Monsoon's bigger sibling. "
                 "It's alternative firmware with thirty modes in two banks, a forty-eight kilohertz "
                 "floating-point engine, and eight controls in every mode: the four sliders, and the STEREO, "
                 "FEEDBACK and REVERB knobs too. Same jacks. Only the weather changes.",
             rows=[R("modes", "30 · two banks", hl=[]), R("engine", "48 kHz · 32-bit float", hl=[]),
                   R("slots", "4 jars of sky", hl=[]), R("look up", "always", hl=[])],
             kind="intro", leds=("meters",)),
        dict(code="ty01", title="the typhoon port", sub="the same thirty skies, a bigger panel",
             desc="Underneath, the Monsoon's Cirrostratus, unchanged. On top, a panel of its own: two meters, "
                  "sixteen lights, an IN and an OUT mute, and an AUX voltage. Some controls have new names.",
             say="The Typhoon port.",
             rows=[R("same engine", "30 modes · same slots",
                     "Underneath, it's the Monsoon's Cirrostratus, note for note: the same thirty modes, the "
                     "same engine, the same save slots and settings.", hl=["sl0", "sl1", "sl2", "sl3"]),
                   R("new panel", "meters · mutes · aux",
                     "On top, a panel of its own: an input and an output meter, sixteen lights, IN and OUT "
                     "mutes, and a random voltage on AUX.", hl=["meters", "in_mute", "out_mute", "aux"]),
                   R("time · bank", "the monsoon's disp and write",
                     "Some controls have new names. TIME is the Monsoon's display button, and BANK is its "
                     "write button.", hl=["time", "bank"]),
                   R("new knob names", "the monsoon's bal, fbk and rvb",
                     "And STEREO, FEEDBACK and REVERB are what the Monsoon calls BAL, FBK and RVB: three "
                     "controls of their own in every mode.", hl=["k1", "k2", "k3"]),
                   R("on a real typhoon", "built and checked in software",
                     "One honest note. This port builds, and it has been checked in software, but it hasn't "
                     "yet run on a real Typhoon. The manual lists what still needs checking.", hl=[])],
             kind="steps", leds=("meters",)),
        dict(code="in02", title="installing", sub="getting the cloud up there",
             desc="Rack off. Computer out into IN L. Hold FREEZE and power on: the FREEZE button blinks. "
                  "Play cirrostratus-typhoon.wav from the start. The TIME and BANK lights come on as each "
                  "block is written.",
             say="Installing.",
             rows=[R("1", "rack off · computer out → IN L",
                     "Switch the rack off, and cable your computer's line out into IN L, the input jack at the "
                     "bottom left. IN R follows it.", hl=["in_l"], leds=("power",)),
                   R("2", "hold FREEZE · power on",
                     "Hold the big white FREEZE button and power on. It blinks: the module is listening.",
                     hl=["freeze"], leds=("power",)),
                   R("3", "volume ¾ · IN LEVEL noon",
                     "Set the computer volume to about three quarters, and IN LEVEL to noon.",
                     hl=["in_level"], leds=("install",)),
                   R("4", "play cirrostratus-typhoon.wav",
                     "Play cirrostratus typhoon dot wav from the start. It takes about six and a half minutes, "
                     "and sounds like a singing modem. FREEZE blinks fast, the meters dance with the level, and "
                     "all the TIME and BANK lights come on each time a block is written.",
                     hl=["meters", "tleds", "bleds"], leds=("install",)),
                   R("blinking?", "press FREEZE, play again",
                     "If the TIME and BANK lights blink, reception failed. Press FREEZE, fix the level, and play "
                     "it again from the start. The bootloader is never touched.",
                     hl=["freeze", "tleds", "bleds"], leds=("install_err",)),
                   R("carried over", "your v/oct calibration",
                     "Your V per octave calibration comes along from the stock firmware. Its save slots don't: "
                     "Cirrostratus simply ignores them.", hl=["voct"], leds=("meters",))],
             kind="steps"),
        dict(code="pn03", title="the panel", sub="what every knob, slider and jack does",
             desc="Here is where everything is on the Typhoon, and what Cirrostratus asks of it.",
             say="The panel. Let's go round it, top to bottom.",
             rows=[R("tune", "pitch · ±4 octaves",
                     "Top left, the big TUNE knob is PITCH: plus or minus four octaves, with gentle magnets on "
                     "every semitone. The V per octave and FREQ jacks add to it.", hl=["tune", "voct", "freq"]),
                   R("in · out mute", "click-free mutes",
                     "IN MUTE and OUT MUTE, the two small buttons, mute the input and the output without a "
                     "click.", hl=["in_mute", "out_mute"]),
                   R("meters", "in on top · out below",
                     "Between them, two rows of four lights: the input meter on top, the output meter below.",
                     hl=["meters"]),
                   R("freeze · hold", "button + gate",
                     "Top right, the big white FREEZE button. Its light is the freeze state. The HOLD jack is "
                     "its gate: a high gate flips whatever the button says.", hl=["freeze", "hold"]),
                   R("in · out level", "analog, as before",
                     "IN LEVEL and OUT LEVEL, either side of the sliders, are analog, like the IN VCA and OUT "
                     "VCA jacks at the two ends of the first jack row.",
                     hl=["in_level", "out_level", "in_vca", "out_vca"]),
                   R("the 4 sliders", "the four sliders · cv below",
                     "The four sliders are POSITION, DENSITY, SIZE and TEXTURE. Each mode gives them its own "
                     "job, and each has a C.V. jack in the first jack row.",
                     hl=["sl0", "sl1", "sl2", "sl3", "cv0", "cv1", "cv2", "cv3"]),
                   R("wet", "dry ↔ wet, equal power",
                     "WET blends dry and wet at equal power, in every mode.", hl=["k0", "kj0"]),
                   R("stereo", "a control of its own, per mode",
                     "STEREO, FEEDBACK and REVERB are three more controls for each mode. STEREO is two-sided: "
                     "noon is the plain sound, and each side does something different.", hl=["k1", "kj1"]),
                   R("feedback", "a control of its own, per mode",
                     "FEEDBACK and REVERB start at zero. Each mode's chapter says what they do there.",
                     hl=["k2", "kj2"]),
                   R("reverb", "a control of its own, per mode",
                     "Each of the four knobs has its C.V. jack right under it.", hl=["k3", "kj3"]),
                   R("bank · a b c d", "load · save · bank flip",
                     "Lower left, BANK, under the A, B, C and D lights: load and save, and in the menu it "
                     "flips banks.", hl=["bank", "bleds"]),
                   R("time · 1 2 4 8", "display · mode menu",
                     "Lower right, TIME, under the one, two, four and eight lights: tap it to change what the "
                     "lights show, hold it for the mode menu.", hl=["time", "tleds"]),
                   R("trig", "jack + button · a job per mode",
                     "TRIG is a jack and a button, and does something different in every mode.",
                     hl=["trig", "trigb"]),
                   R("aux", "behind every cv jack",
                     "AUX feeds every unpatched C.V. jack. More about that later.", hl=["aux"]),
                   R("in l r · out l r", "stereo audio",
                     "Stereo in at the bottom left, stereo out at the bottom right.",
                     hl=["in_l", "in_r", "out_l", "out_r"])],
             kind="panel"),
        dict(code="mn04", title="choosing a mode", sub="hold time, move position, let go",
             desc="Hold TIME: the 1 2 4 8 lights show the mode in binary, A for bank one, B for bank two. "
                  "Move POSITION to browse, tap BANK to flip banks, tap FREEZE for the mode's options, "
                  "and let go of TIME.",
             say="Choosing a mode.",
             rows=[R("hold time", "menu opens",
                     "Hold TIME. The one, two, four and eight lights show the mode as a binary number, and A or "
                     "B shows the bank: A for bank one, modes one to fifteen, B for bank two, sixteen to "
                     "thirty.", hl=["time", "tleds", "bleds"], leds=("menu",)),
                   R("position", "browse the bank",
                     "Move the POSITION slider to browse. Only the slider: its C.V. is ignored here.",
                     hl=["sl0"], leds=("menu",)),
                   R("tap bank", "other bank", "Tap BANK to flip banks.", hl=["bank"], leds=("menu",)),
                   R("tap freeze", "options",
                     "Tap FREEZE to step through the mode's options. The option number blinks in binary on "
                     "the TIME lights.", hl=["freeze", "tleds"], leds=("option", 2)),
                   R("let go", "fade in", "Let go of TIME, and the new weather fades in.",
                     hl=["time"], leds=("mode", 2))],
             kind="menu"),
        dict(code="mt05", title="looking at the sky", sub="the meters, and the knobs page",
             desc="The meters are always on: input on top, output below. Tap TIME for the knobs page, where "
                  "A to D show WET, STEREO, FEEDBACK and REVERB. The FREEZE light is the freeze state.",
             say="Looking at the sky.",
             rows=[R("in meter", "green · green · yellow · red",
                     "The top row of lights is the input meter: green, green, yellow, red.", hl=["meters"],
                     leds=("meters",)),
                   R("out meter", "after OUT LEVEL and its vca",
                     "The second row is the output meter. Like the stock firmware, it follows OUT LEVEL and "
                     "the OUT VCA.", hl=["meters", "out_level", "out_vca"], leds=("meters",)),
                   R("the mode, dimly", "on the time and bank lights",
                     "Underneath, the TIME and BANK lights show the running mode, dimly.",
                     hl=["tleds", "bleds"], leds=("meters",)),
                   R("tap time", "the knobs page",
                     "Tap TIME for the knobs page: A, B, C and D glow as far as WET, STEREO, FEEDBACK and "
                     "REVERB are turned up. Tap it again to go back.",
                     hl=["time", "bleds", "k0", "k1", "k2", "k3"], leds=("knobs",)),
                   R("freeze light", "the freeze state, always",
                     "And the FREEZE light is the freeze state, always.", hl=["freeze"], leds=("meters",))],
             kind="look"),
        dict(code="sl06", title="four jars of sky", sub="save slots and slot morph",
             desc="Hold BANK to save, tap BANK to load. TIME picks the slot, on the A to D lights. Each mode "
                  "keeps what matters: a buffer, a spectrum, a wavetable, a tape. On the load page, FREEZE "
                  "arms a slot as a morph source for SPECTRAL and CAPTURE.",
             say="Four jars of sky. Each mode keeps what matters to it: a recorded buffer, a frozen spectrum, "
                 "a wavetable, a tape loop.",
             rows=[R("tap bank", "load",
                     "Tap BANK for the load page. The slot's light blinks, on A to D.", hl=["bank", "bleds"],
                     leds=("load", 0)),
                   R("time", "pick a slot", "TIME picks the slot, and BANK loads it.", hl=["time", "bleds"],
                     leds=("load_cycle",)),
                   R("hold bank", "save",
                     "Hold BANK for the save page. The slot blinks, and the other three glow dimly. TIME picks, "
                     "BANK saves.", hl=["bank", "bleds"], leds=("save", 1)),
                   R("freeze (load page)", "arm morph source",
                     "On the load page, tap FREEZE instead, and that slot becomes a morph source that Spectral "
                     "and Capture can blend toward. Its light stays on.", hl=["freeze", "bleds"],
                     leds=("load", 2, 0))],
             kind="slots"),
        dict(code="ev07", title="weather that follows you", sub="the envelope page",
             desc="Hold FREEZE and tap TIME. Your input's loudness can push WET, STEREO, FEEDBACK or REVERB "
                  "up or down. Row 1/A is WET, 2/B STEREO, 4/C FEEDBACK, 8/D REVERB: a BANK light is up, a "
                  "TIME light is down, dim is half.",
             say="Weather that follows you. Hold FREEZE and tap TIME to open the envelope page. Your input's "
                 "loudness can push any of the four knobs up or down, whatever they do in the mode. Each row "
                 "of lights is one knob: one and A for WET, two and B for STEREO, four and C for FEEDBACK, "
                 "eight and D for REVERB. A lit BANK light pushes up, a lit TIME light pushes down: dim is "
                 "half, full is one. TIME moves along, FREEZE sets the amount, BANK saves.",
             rows=[R("wet", "+1", "A wet that opens on loud notes.", hl=["k0", "bleds"], leds=("env", 0)),
                   R("stereo", "off", "STEREO left alone.", hl=["k1", "bleds", "tleds"], leds=("env", 1)),
                   R("feedback", "+½", "FEEDBACK that swells a little.", hl=["k2", "bleds"], leds=("env", 2)),
                   R("reverb", "−1", "And REVERB that backs off while you play.", hl=["k3", "tleds"],
                     leds=("env", 3))],
             kind="env", leds=("env",)),
        dict(code="mu08", title="hush", sub="the in and out mutes",
             desc="Tap IN MUTE or OUT MUTE: a click-free mute with a five millisecond fade. IN mutes before "
                  "the engine, so tails ring on; OUT mutes the whole output. A muted row's red light blinks.",
             say="Hush. The Typhoon has two mute buttons, and Cirrostratus uses both.",
             rows=[R("in mute", "before the engine",
                     "Tap IN MUTE to mute the input, with a five millisecond fade. It mutes before the "
                     "engine, so tails, frozen sounds and voices keep sounding.", hl=["in_mute"], leds=("mute", 1, 0)),
                   R("out mute", "the whole output",
                     "Tap OUT MUTE to mute the module's output, after the engine, the mono bass and the "
                     "limiter.", hl=["out_mute"], leds=("mute", 0, 1)),
                   R("red light", "blinks while muted",
                     "While a side is muted, the red light at the end of its meter row blinks, and the rest of "
                     "the row goes dark.", hl=["meters"], leds=("mute", 1, 1)),
                   R("power-up", "both off",
                     "The mutes aren't saved: both are off each time you power up.", hl=["in_mute", "out_mute"],
                     leds=("meters",)),
                   R("which is which", "not yet checked on a module",
                     "Which button is which comes from the stock firmware, and hasn't been checked on a real "
                     "Typhoon yet.", hl=["in_mute", "out_mute"], leds=("meters",))],
             kind="mute"),
        dict(code="ax09", title="drifting air", sub="the aux random voltage",
             desc="AUX is the normal behind nine CV jacks. Off, it's 0 V. Hold IN MUTE and OUT MUTE for a "
                  "second, and it becomes a slow random voltage that nudges every unpatched CV.",
             say="Drifting air: the AUX random voltage.",
             rows=[R("aux", "the normal of nine cv jacks",
                     "AUX is the voltage behind all nine C.V. jacks: every one except V per octave.",
                     hl=["aux", "cv0", "cv1", "cv2", "cv3", "kj0", "kj1", "kj2", "kj3", "freq"],
                     leds=("meters",)),
                   R("off", "0 V · the default",
                     "Off, the default, it's zero volts, so an unpatched jack does nothing.", hl=["aux"],
                     leds=("aux", 0)),
                   R("in + out mute 1 s", "on / off",
                     "Hold IN MUTE and OUT MUTE together for a second to switch it. All the TIME and BANK "
                     "lights mean on; the eight and D lights alone mean off. The setting is saved.",
                     hl=["in_mute", "out_mute", "tleds", "bleds"], leds=("aux", 1)),
                   R("on", "a slow random drift",
                     "On, it's a slow random voltage: a new value every half second, gliding there. Every "
                     "unpatched C.V. jack drifts up by as much as a quarter of its range.",
                     hl=["sl0", "sl1", "sl2", "sl3", "k0", "k1", "k2", "k3"], leds=("aux", 1)),
                   R("a cable in aux", "your own cv instead",
                     "A cable in AUX replaces it with your own C.V., for every unpatched jack at once.",
                     hl=["aux"], leds=("meters",))],
             kind="aux"),
        dict(code="su10", title="sunrise", sub="what the lights do when it starts",
             desc="Ice crystals twinkle across all sixteen lights. Halos pulse out from the middle of each "
                  "group. The sun breaks through. Then the forecast: your mode, fading out.",
             say="Sunrise. Every time you power up: ice crystals twinkle across all sixteen lights, two halos "
                 "pulse outward from the middle of each group while the FREEZE button glows, the sun breaks "
                 "through from half to full, and then the forecast, your current mode on the TIME and BANK "
                 "lights, fades away. Any button skips it.",
             rows=[R("0.0 s", "ice crystals", hl=["meters", "tleds", "bleds"]),
                   R("0.8 s", "halo", hl=["meters", "freeze"]),
                   R("1.9 s", "the sun breaks through", hl=["meters", "tleds", "bleds"]),
                   R("2.5 s", "the forecast", hl=["tleds", "bleds"])], kind="sunrise"),
        dict(code="os11", title="the output section", sub="hard stereo, mono bass",
             desc="Hard stereo: left is left, right is right, exactly what the mode makes. Below sixty "
                  "hertz the main out is summed to mono, on by default. An equal-power blend, and a "
                  "limiter that stays out of the way.",
             say="The output section.",
             rows=[R("wet", "equal-power blend", "WET blends dry and wet at equal power.", leds=("meters",)),
                   R("hard stereo", "exactly what the mode makes",
                     "The output is hard stereo. Modes that are wide, are wide on their own.",
                     hl=["out_l", "out_r"], leds=("meters",)),
                   R("mono bass", "< 60 Hz mono · on by default",
                     "Below sixty hertz the whole main out is summed to mono, so your sub stays in the "
                     "middle of the room.", hl=["out_l", "out_r"], leds=("monobass", 1)),
                   R("freeze + bank", "mono bass on / off",
                     "Hold FREEZE and tap BANK to switch it. The middle lights, two and four, B and C, mean on. "
                     "The outer ones, one and eight, A and D, mean off.",
                     hl=["freeze", "bank", "tleds", "bleds"], leds=("monobass", 0)),
                   R("limiter", "−1 dBFS", "And a limiter that stays out of the way.", leds=("meters",))],
             kind="output"),
        dict(code="bq12", title="longer buffers", sub="buffer quality, for every mode that records",
             desc="Hold BANK and tap TIME. FREEZE steps through 48, 24 and 16 kHz, in 16 or 8 bit. "
                  "The lower settings hold up to six times more sound in STRETCH, CAPTURE, TAPE, "
                  "BARBERPOLE and BLOOM.",
             say="Longer buffers. One master setting for every mode that records. "
                 "Trade a little fidelity, for up to six times more sound.",
             rows=[R("bank + time", "open the buffer page",
                     "Hold BANK and tap TIME. A light runs down the TIME column, and the eight light stays on "
                     "while you're on the page.", hl=["bank", "time", "tleds"], leds=("bq_sweep",)),
                   R("freeze", "48 · 24 · 16 kHz",
                     "FREEZE steps through the settings. One, two or three TIME lights: forty-eight, "
                     "twenty-four or sixteen kilohertz.", hl=["freeze", "tleds"], leds=("bq_cycle",)),
                   R("a", "16-bit · lower rates lose the top",
                     "A is sixteen bit. The lower rates simply lose the very top end, like dusk coming in.",
                     hl=["bleds"], leds=("bq", 2, True)),
                   R("b", "8-bit · twice as long again",
                     "B is eight bit: twice as long again, with a fine grain of noise under the loud parts.",
                     hl=["bleds"], leds=("bq", 5, True)),
                   R("8 blinking", "a change is pending",
                     "While the eight light blinks, BANK would change something.", hl=["tleds"],
                     leds=("bq", 5, True)),
                   R("bank · time", "apply · cancel",
                     "BANK applies it, and the running mode's buffer starts again, empty. Or tap TIME to leave "
                     "without changing a thing.", hl=["bank", "time"], leds=("bq_applied", 5)),
                   R("tape mono", "1.2 s → 7.2 s",
                     "Tape runs slower, so in mono its echoes go from just over a second, to seven. "
                     "Stretch, up to five.", hl=["sl0", "sl1", "sl2", "sl3"], leds=("bq", 5)),
                   R("slots", "keep their quality",
                     "And a save slot remembers the quality it was recorded with.", hl=["bank", "bleds"],
                     leds=("bq", 5))],
             kind="buffer"),
        dict(code="ff13", title="fft size", sub="one resolution for every spectral mode",
             desc="Hold TIME and tap FREEZE. FREEZE steps through each mode's own size, then 256, 512, 1024 "
                  "and 2048 points, for SPECTRAL and SMEAR, CROSS, STRETCH, PUTZ, QUELL, SPECDELAY and MIRROR. "
                  "BANK applies, TIME cancels.",
             say="F F T size. One setting for the resolution of every spectral mode.",
             rows=[R("time + freeze", "open the fft page",
                     "Hold TIME and tap FREEZE. A light runs up the TIME column: you're on the page.",
                     hl=["time", "freeze", "tleds"], leds=("fft_sweep",)),
                   R("a alone", "each mode's own size",
                     "A alone is the default: each mode at its own size, exactly as before. The TIME lights "
                     "show that size as a bar.", hl=["bleds", "tleds"], leds=("fft", 0)),
                   R("freeze", "256 · 512 · 1024 · 2048",
                     "FREEZE steps through the sizes: a bar of one to four TIME lights, two hundred and "
                     "fifty-six points up to two thousand and forty-eight, with A and B both lit: one size "
                     "for all.", hl=["freeze", "tleds", "bleds"], leds=("fft_cycle",)),
                   R("small", "sharp, grainy, quick",
                     "Small sizes are sharp and quick: transients survive, spectra turn grainy.",
                     hl=["tleds"], leds=("fft", 1, True)),
                   R("large", "smooth, glassy",
                     "Large sizes are smooth and glassy, with finer pitch.", hl=["tleds"],
                     leds=("fft", 4, True)),
                   R("bank · time", "apply · cancel",
                     "BANK applies it, the lights flash, and the running spectral mode restarts. TIME leaves "
                     "without a change. Blur, decay and delay times stay the same at every size: only the "
                     "resolution changes.", hl=["bank", "time"], leds=("fft_applied", 4)),
                   R("slots", "keep their size",
                     "A frozen spectrum saved in a slot comes back at the size it was saved with.",
                     hl=["bank", "bleds"], leds=("fft", 4))],
             kind="buffer"),
        dict(code="ca14", title="teaching it volts", sub="v/oct calibration",
             desc="Hold TRIG while you power on, or hold TIME and then BANK for a second. Send 1 V to "
                  "V/OCT, tap TRIG; send 3 V, tap TRIG. Leave the other CV jacks unpatched.",
             say="Teaching it volts. Your calibration comes along from the stock firmware, so you may never "
                 "need this. If you do, there are two ways in.",
             rows=[R("trig at power-up", "the stock way",
                     "Hold the TRIG button while you power on. The input row blinks.", hl=["trigb", "meters"],
                     leds=("cal", 1)),
                   R("or time, then bank", "the monsoon way",
                     "Or hold TIME, and once the menu is open, hold BANK too, for a second.",
                     hl=["time", "bank"], leds=("cal", 1)),
                   R("1 V", "into v/oct · tap TRIG",
                     "Send one volt into V per octave and tap TRIG. Both rows blink.", hl=["voct", "trigb"],
                     leds=("cal", 2)),
                   R("3 V", "tap TRIG · done",
                     "Send three volts and tap TRIG. Done. BANK confirms too.", hl=["voct", "trigb", "bank"],
                     leds=("cal", 2)),
                   R("other jacks", "leave them unpatched",
                     "Leave the other C.V. jacks unpatched: the last step also learns where their zero volts "
                     "are. AUX is held at zero while you calibrate.",
                     hl=["cv0", "cv1", "cv2", "cv3", "kj0", "kj1", "kj2", "kj3"], leds=("cal", 2)),
                   R("all lit?", "try again",
                     "If all sixteen lights come on, the readings looked wrong and nothing was stored. Check "
                     "the voltages and try again.", hl=["meters", "tleds", "bleds"], leds=("panic",))],
             kind="cal"),
    ]


def build_chapters():
    ch = general_chapters()
    for m in bv.parse_modes(typhoon_text.typhoonize):
        code = ("cs" if m["num"] <= 15 else "cr") + "%02d" % m["num"]
        rows = []
        for label, text in m["rows"]:
            if label.lower().startswith("keeps"):
                continue
            spoken = "%s: %s." % (label.replace("(hold)", "held"), text)
            rows.append(bv.row(label, text, spoken))
        say = "Mode %d. %s. %s. %s. %s" % (m["num"], m["name"].lower(), m["sub"], m["forecast"].rstrip("."),
                                         m["desc"])
        ch.append(dict(code=code, title=m["name"].lower(), sub=m["sub"].lower(), desc=m["desc"],
                       forecast=m["forecast"], say=say, rows=rows, kind="mode", num=m["num"]))
    ch.append(dict(code="cs∞", title="look up", sub="cirrostratus · firmware for typhoon",
                   desc="Thirty kinds of sky, now on the Typhoon. Thanks to Mutable Instruments, Tom Burns, "
                        "Jak Plugg, Calsynth and SuperParasites.",
                   say="That's Cirrostratus on the Typhoon. Thirty kinds of sky, in one big cloud. Thanks to "
                       "Mutable Instruments, Tom Burns, Jak Plugg, Calsynth and the SuperParasites firmware. "
                       "Now go and look up.",
                   rows=[bv.row("modes", "30", hl=[]), bv.row("tests", "231 passing", hl=[]),
                         bv.row("licence", "mit", hl=[])], kind="outro", leds=("sunrise",)))
    for i, c in enumerate(ch):
        c["index"] = i
        c["ground"] = bv.hexrgb(bv.GROUNDS[i % len(bv.GROUNDS)])
        c["accent"] = bv.hexrgb(bv.ACCENTS[i % len(bv.ACCENTS)])
    return ch


def chapter_title(c):
    """YouTube chapter titles, as in the Monsoon's chapters file."""
    if c["kind"] == "intro":
        return "Intro – Cirrostratus, " + c["sub"]
    if c["kind"] == "mode":
        return "Mode %d – %s · %s" % (c["num"], c["title"].upper(), c["sub"])
    title = c["title"][0].upper() + c["title"][1:]
    return "%s – %s" % (title.replace("Fft", "FFT"), c["sub"])


def short_chapters():
    raise SystemExit("--short is the Monsoon's 90-second poem; there is no Typhoon version")


_SPEAKABLE = None  # build_video's own speakable()


def speakable_typhoon(s):
    s = re.sub(r"\bAUX\b", "aux", s)
    s = re.sub(r"\bVCA\b", "V.C.A.", s)
    s = re.sub(r"\bFREQ\b", "frequency", s)
    return _SPEAKABLE(s)


def install(module):
    """Patch build_video (module) for the Typhoon."""
    global bv, F_LAB, F_LAB_S, _SPEAKABLE
    bv = module
    if module.speakable is not speakable_typhoon:
        _SPEAKABLE = module.speakable
    F_LAB = bv.font("IBMPlexMono-Medium.ttf", 17)
    F_LAB_S = bv.font("IBMPlexMono-Medium.ttf", 14)
    for name in ("PANEL", "PANEL_W", "PANEL_H", "PANEL_X", "PANEL_Y", "PANEL_S", "SL_X", "SL_TOP", "SL_BOT",
                 "KN_X", "LED_X", "KNOB_ANGLE", "P", "circ", "ptext", "panel_static", "panel_dynamic",
                 "draw_connector", "draw_rows", "hl_for", "chapter_leds", "build_chapters", "short_chapters"):
        setattr(bv, name, globals()[name])
    bv.speakable = speakable_typhoon
    bv.HEADER = "cirrostratus · firmware for typhoon"
    bv.DEFAULT_OUT = "~/Desktop/Cirrostratus-Typhoon-Video-Manual.mp4"
    bv.CHAPTER_TITLE = chapter_title
