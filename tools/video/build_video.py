#!/usr/bin/env python3
"""Cirrostratus video manual, in the faltung look (deep-blue edition).

  python3 tools/video/build_video.py --music ~/Desktop/Njari/Bliss.wav \
      --out docs/Cirrostratus-Video-Manual.mp4 [--only 0-3] [--preview]

Every chapter is one flat deep-blue field: a mono code above a lowercase
title, the text, faltung sliders that move, a turning wireframe
icosahedron with one white face, folded strips with codes on them, a stdio
tracklist, and the module's LED row. The text is spoken (macOS `say`) and
the music sits underneath, ducked while the voice speaks.

Content comes from the manual (docs/MANUAL.md, docs/modes_*.md), so the
video always matches it. Needs Pillow, numpy, ffmpeg.
"""
import argparse, math, os, re, subprocess, sys, tempfile, wave
from multiprocessing import Pool

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONTS = os.path.expanduser("~/faltung-kit/assets/fonts")
W, H, FPS = 1920, 1080, 30
SR = 48000

# ---------------------------------------------------------------------------
# Palette: deep-blue grounds (faltung deep, computed the faltung way: same
# hue, lightness 0.085-0.17) with light accents. Ink is the faltung light ink.
INK = (246, 246, 240)
GROUNDS = ["#0f1a2e", "#13203a", "#14192d", "#122334", "#101a2c", "#172541",
           "#0e1830", "#152236", "#111d33", "#18213b"]
ACCENTS = ["#5b7aa3", "#95c6db", "#6e80c8", "#4a9ecf", "#71bbdf", "#8d9bd6",
           "#9fd3ec", "#5e71be"]
STRIPS = ["#2c4a7a", "#3b5f96", "#5b7aa3", "#6e80c8", "#95c6db", "#cfe6f0",
          "#4a9ecf", "#8d9bd6"]
LED_GREEN = (110, 230, 120)
LED_RED = (255, 90, 80)


def hexrgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


F_TITLE = font("Inter-Medium.ttf", 96)
F_SUB = font("Inter-Regular.ttf", 32)
F_BODY = font("Inter-Regular.ttf", 27)
F_MONO = font("IBMPlexMono-Regular.ttf", 23)
F_MONO_S = font("IBMPlexMono-Regular.ttf", 19)
F_MONO_M = font("IBMPlexMono-Medium.ttf", 24)
F_CODE = font("IBMPlexMono-Medium.ttf", 26)

# ---------------------------------------------------------------------------
# Content.

def clean_md(s):
    s = re.sub(r"\*\*(.+?)\*\*", r"\1", s)
    s = re.sub(r"\*(.+?)\*", r"\1", s)
    s = re.sub(r"`(.+?)`", r"\1", s)
    return s.strip()


def speakable(s):
    s = clean_md(s)
    for a, b in [("→", " to "), ("←", " to "), ("↔", " to "), ("·", ","), ("×", " times "),
                 ("±", "plus or minus "), ("≈", "about "), ("V/OCT", "V per octave"),
                 ("V/oct", "V per octave"), ("½", "half"), ("¼", "a quarter"), ("¾", "three quarters"),
                 ("e.g.", "for example"), ("—", ","), ("–", " to "), ("/", " or "), ("%", " percent"),
                 ("dBFS", "dB"), ("IR", "I.R."), ("LED", "L.E.D."), ("CV", "C.V."), ("FDB", "feedback"),
                 ("RVB", "reverb"), ("STER", "stereo"), ("DISP", "display"), ("TRIG", "trig"),
                 ("FFT", "F.F.T."), ("SWD", "S.W.D."), ("DSP", "D.S.P.")]:
        s = s.replace(a, b)
    s = re.sub(r"\s+", " ", s)
    return s.strip()


def parse_modes():
    """Mode chapters from docs/modes_*.md."""
    modes = []
    for path in sorted(p for p in os.listdir(os.path.join(ROOT, "docs")) if p.startswith("modes_")):
        text = open(os.path.join(ROOT, "docs", path)).read()
        for block in re.split(r"\n(?=#### )", text):
            m = re.match(r"#### (\d+) · (\S+) — (.+)", block)
            if not m:
                continue
            num, name, sub = int(m.group(1)), m.group(2), m.group(3).strip()
            lines = block.split("\n")[1:]
            forecast = ""
            para = []
            rows = []
            for ln in lines:
                ln = ln.strip()
                if ln.startswith("*Forecast:"):
                    forecast = clean_md(ln).replace("Forecast:", "").strip()
                elif ln.startswith("|") and not ln.startswith("|---") and not ln.startswith("| Control"):
                    cells = [c.strip() for c in ln.strip("|").split("|")]
                    if len(cells) >= 2 and cells[0]:
                        rows.append((clean_md(cells[0]), clean_md(cells[1])))
                elif ln and not ln.startswith("#") and not ln.startswith("|") and not ln.startswith("On the module"):
                    if not rows:
                        para.append(ln)
            desc = clean_md(" ".join(para))
            modes.append(dict(num=num, name=name, sub=sub, forecast=forecast, desc=desc, rows=rows))
    modes.sort(key=lambda d: d["num"])
    return modes


def general_chapters():
    return [
        dict(code="cs00", title="cirrostratus", sub="a thin, high sheet of cloud for the monsoon",
             desc="Alternative firmware for Jak Plugg's Monsoon, the twelve HP Clouds. Thirty modes in two "
                  "banks, a forty-eight kilohertz floating-point engine, and a new output section on every mode.",
             say="Cirrostratus. A thin, high sheet of cloud for the Monsoon. It's alternative firmware for "
                 "Jak Plugg's Monsoon: thirty modes in two banks, a forty-eight kilohertz floating-point engine, "
                 "and a new output section on every mode. Same panel, same jacks. Only the weather changes.",
             rows=[("modes", "30 · two banks"), ("engine", "48 kHz · 32-bit float"), ("slots", "4 jars of sky"),
                   ("look up", "always")], kind="intro"),
        dict(code="in01", title="installing", sub="getting the cloud up there",
             desc="Rack off. Computer out into IN L. Hold FREEZE and power on: the FREEZE LED blinks. "
                  "Play cirrostratus.wav from the start. Four green LEDs as each block is written; "
                  "a little rain when it's done.",
             say="Installing. Switch the rack off, and cable your computer's output into IN L. Hold FREEZE and "
                 "power on: the FREEZE light blinks, the module is listening. Set the volume to about three "
                 "quarters and IN LEVEL to noon, then play cirrostratus dot wav from the start. It sounds like "
                 "a singing modem. The lights dance with the level, and all four turn green each time a block "
                 "is written. If they blink red, press FREEZE and play it again. Nothing can break: the "
                 "bootloader is never touched.",
             rows=[("1", "rack off · computer out → IN L"), ("2", "hold FREEZE · power on"),
                   ("3", "volume ¾ · IN LEVEL noon"), ("4", "play cirrostratus.wav"),
                   ("red?", "press FREEZE, play again")], kind="steps"),
        dict(code="pn02", title="the panel", sub="four sliders, one big knob, four small ones",
             desc="The sliders are POSITION, DENSITY, SIZE and TEXTURE. The big knob and V/OCT are PITCH. "
                  "The small knobs are the output section: WET, STEREO, FEEDBACK and REVERB.",
             say="The panel. The four sliders are POSITION, DENSITY, SIZE and TEXTURE. The big knob and the "
                 "V per octave input are PITCH, with gentle magnets on every semitone. The four small knobs are "
                 "the output section on every mode: wet, stereo, feedback and reverb. FREEZE, TRIG, DISP and the "
                 "pencil button do the rest.",
             rows=[("position", "slider + cv"), ("density", "slider + cv"), ("size", "slider + cv"),
                   ("texture", "slider + cv"), ("pitch", "big knob + v/oct"), ("wet stereo fdb rvb", "output section")],
             kind="panel"),
        dict(code="mn03", title="choosing a mode", sub="hold disp, move position, let go",
             desc="Hold DISP: the LEDs show the mode in binary, green for bank one, red for bank two. "
                  "Move POSITION to browse, tap WRITE to flip banks, tap FREEZE for the mode's options, "
                  "and let go of DISP.",
             say="Choosing a mode. Hold DISP. The four lights show the mode as a binary number: green for bank "
                 "one, modes one to fifteen, red for bank two, sixteen to thirty. Move POSITION to browse. Tap "
                 "WRITE to flip banks, and tap FREEZE to step through the mode's options. Let go of DISP, and "
                 "the new weather fades in.",
             rows=[("hold disp", "menu opens"), ("position", "browse 15 modes"), ("tap write", "other bank"),
                   ("tap freeze", "options"), ("let go", "fade in")], kind="menu"),
        dict(code="sl04", title="four jars of sky", sub="save slots and slot morph",
             desc="Hold WRITE to save, tap WRITE to load. DISP picks the slot. Each mode keeps what matters: "
                  "a buffer, a spectrum, a wavetable, a tape. On the load page, FREEZE arms a slot as a morph "
                  "source for SPECTRAL and CAPTURE.",
             say="Four jars of sky. Hold WRITE to save, tap WRITE to load, and DISP picks the slot. Each mode "
                 "keeps what matters to it: a recorded buffer, a frozen spectrum, a wavetable, a tape loop. On "
                 "the load page, tap FREEZE instead, and that slot becomes a morph source that Spectral and "
                 "Capture can blend toward.",
             rows=[("hold write", "save"), ("tap write", "load"), ("disp", "pick a slot"),
                   ("freeze (load page)", "arm morph source")], kind="slots"),
        dict(code="ev05", title="weather that follows you", sub="the envelope page",
             desc="Hold FREEZE and tap DISP. Your input's loudness can push WET, STEREO, FEEDBACK or REVERB "
                  "up or down: off, plus a half, plus one, minus a half, minus one.",
             say="Weather that follows you. Hold FREEZE and tap DISP to open the envelope page. Your input's "
                 "loudness can push any of the four small knobs up or down. A reverb that ducks while you play. "
                 "A wet that opens on loud notes. DISP moves along, FREEZE sets the amount, WRITE saves.",
             rows=[("wet", "+1"), ("stereo", "off"), ("feedback", "+½"), ("reverb", "−1")], kind="env"),
        dict(code="su06", title="sunrise", sub="what the lights do when it starts",
             desc="Ice crystals twinkle. Two amber halos pulse outward. The sun breaks through, amber to green. "
                  "Then the forecast: your current mode, in binary, fading out.",
             say="Sunrise. Every time you power up: ice crystals twinkle, two amber halos pulse outward from "
                 "the middle, the sun breaks through from amber to green, and then the forecast, your current "
                 "mode in binary, fades away. Any button skips it.",
             rows=[("0.0 s", "ice crystals"), ("0.8 s", "halo"), ("1.9 s", "the sun breaks through"),
                   ("2.5 s", "the forecast")], kind="sunrise"),
        dict(code="os07", title="the output section", sub="on every mode",
             desc="Feedback with manners: a resonance tamer hunts the loudest narrow peaks inside the loop. "
                  "A mono-safe widener, an equal-power blend, an eight-line reverb, and a limiter that stays "
                  "out of the way.",
             say="The output section, on every mode. Feedback with manners: inside the loop, two adaptive "
                 "notches hunt the loudest narrow peaks, so a howl blooms instead of shrieking. A widener that "
                 "survives mono, an equal-power blend, an eight-line reverb that's one knob for size and send, "
                 "and a limiter that stays out of the way.",
             rows=[("feedback", "tamed"), ("stereo", "mono-safe width"), ("wet", "equal power"),
                   ("reverb", "8-line fdn · 0.5–16 s"), ("limiter", "−1 dBFS")], kind="output"),
    ]


def build_chapters():
    ch = general_chapters()
    for m in parse_modes():
        code = ("cs" if m["num"] <= 15 else "cr") + "%02d" % m["num"]
        free = [r for r in m["rows"] if r[0].lower() not in ("keeps", "keeps / morph")]
        frz = next((r[1] for r in m["rows"] if r[0].lower().startswith("freeze")), "")
        trg = next((r[1] for r in m["rows"] if r[0].lower() == "trig"), "")
        say = "Mode %d. %s. %s. %s. %s" % (m["num"], m["name"].lower(), m["sub"], m["forecast"], m["desc"])
        if frz:
            say += " Freeze: " + frz + "."
        if trg:
            say += " Trig: " + trg + "."
        ch.append(dict(code=code, title=m["name"].lower(), sub=m["sub"].lower(), desc=m["desc"],
                       forecast=m["forecast"], say=say, rows=free, kind="mode", num=m["num"]))
    ch.append(dict(code="cs∞", title="look up", sub="cirrostratus · firmware for monsoon",
                   desc="Thirty kinds of sky. Thanks to Mutable Instruments, Tom Burns and Jak Plugg.",
                   say="That's Cirrostratus. Thirty kinds of sky, in one small cloud. Thanks to Mutable Instruments, "
                       "Tom Burns and Jak Plugg. Now go and look up.",
                   rows=[("modes", "30"), ("tests", "181 passing"), ("licence", "mit")], kind="outro"))
    for i, c in enumerate(ch):
        c["index"] = i
        c["ground"] = hexrgb(GROUNDS[i % len(GROUNDS)])
        c["accent"] = hexrgb(ACCENTS[i % len(ACCENTS)])
    return ch


# ---------------------------------------------------------------------------
# Drawing.

PHI = (1 + 5 ** 0.5) / 2
ICO_V = np.array([(-1, PHI, 0), (1, PHI, 0), (-1, -PHI, 0), (1, -PHI, 0), (0, -1, PHI), (0, 1, PHI),
                  (0, -1, -PHI), (0, 1, -PHI), (PHI, 0, -1), (PHI, 0, 1), (-PHI, 0, -1), (-PHI, 0, 1)], float)
ICO_V /= np.linalg.norm(ICO_V[0])
ICO_F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
         (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
         (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]


def draw_ico(d, t, cx, cy, r, col, white_face=0):
    a, b = t * 0.35, t * 0.22 + 0.4
    ry = np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])
    rx = np.array([[1, 0, 0], [0, math.cos(b), -math.sin(b)], [0, math.sin(b), math.cos(b)]])
    v = ICO_V @ ry.T @ rx.T
    pts = [(cx + p[0] * r, cy - p[1] * r) for p in v]
    f = ICO_F[white_face]
    n = np.cross(v[f[1]] - v[f[0]], v[f[2]] - v[f[0]])
    if n[2] > 0:
        d.polygon([pts[k] for k in f], fill=INK)
    edges = set()
    for f in ICO_F:
        for i in range(3):
            e = tuple(sorted((f[i], f[(i + 1) % 3])))
            edges.add(e)
    for e in edges:
        z = (v[e[0]][2] + v[e[1]][2]) / 2
        c = mix(col, INK, 0.35) if z > 0 else mix(col, (0, 0, 0), 0.35)
        d.line([pts[e[0]], pts[e[1]]], fill=c, width=3 if z > 0 else 1)


def skew_rect(d, x, y, w, h, fill, lean=0.55):
    o = h * lean
    d.polygon([(x + o, y), (x + w + o, y), (x + w, y + h), (x, y + h)], fill=fill)


def draw_strips(d, ox, oy, t, code):
    """snd folded strips: isometric bands with codes on them."""
    for i in range(6):
        c = hexrgb(STRIPS[(i + code) % len(STRIPS)])
        x = ox + i * 46
        y = oy + 18 * math.sin(t * 0.5 + i * 0.7)
        L = 105 - i * 9
        # one fold: up-right then down-right
        p = [(x, y), (x + 40, y - 23), (x + 40, y - 23 + L), (x, y + L)]
        d.polygon(p, fill=c)
        d.polygon([(x + 40, y - 23), (x + 80, y), (x + 80, y + L), (x + 40, y - 23 + L)],
                  fill=mix(c, (0, 0, 0), 0.28))


def led_row(d, x, y, states, freeze=False, label=True):
    """states: list of 4 (r, g) 0..1 tuples."""
    for i, (r, g) in enumerate(states):
        cx = x + i * 64
        col = (int(min(255, 40 + 215 * r * (1 if g < 0.01 else 1.0))), int(40 + 215 * g), 50)
        if r < 0.01 and g < 0.01:
            col = (40, 48, 66)
        d.ellipse([cx - 17, y - 17, cx + 17, y + 17], fill=col)
        if r > 0.01 or g > 0.01:
            d.ellipse([cx - 27, y - 27, cx + 27, y + 27], outline=mix(col, (0, 0, 0), 0.4), width=2)
    fx = x + 4 * 64 + 30
    d.ellipse([fx - 11, y - 11, fx + 11, y + 11], fill=(250, 250, 250) if freeze else (40, 48, 66))
    if label:
        d.text((x - 17, y + 34), "leds", font=F_MONO_S, fill=mix(INK, (0, 0, 0), 0.45))
        d.text((fx - 30, y + 34), "freeze", font=F_MONO_S, fill=mix(INK, (0, 0, 0), 0.45))


def binary_leds(n, green):
    return [((0.0, 1.0) if green else (1.0, 0.0)) if (n >> (3 - i)) & 1 else (0.0, 0.0) for i in range(4)]


def splash_leds(ms):
    """Python port of Ui::PaintSplash (monsoon/ui.cc), for the sunrise chapter."""
    st = [(0.0, 0.0)] * 4
    frz = False
    t = int(ms)
    def tri(tt, period):
        if tt < 0 or tt >= period: return 0
        h = period // 2
        return (tt * 255 // h if tt < h else (period - tt) * 255 // h)
    if t < 800:
        out = []
        for i in range(4):
            hsh = ((t // 90 + i * 7919) * 2654435761) & 0xffffffff
            lvl = ((hsh >> 16) & 0x7f) if (hsh >> 24) > 170 else 0
            lvl = lvl * min(t, 400) // 400
            out.append((lvl / 3 / 255, lvl / 255))
        return out, False
    if t < 1900:
        u = t - 800
        st = [(0.0, 0.0)] * 4
        glow = 0
        for pulse in range(2):
            p = u - pulse * 500
            inner, outer = tri(p, 400), tri(p - 150, 400)
            if inner or outer:
                st = [(outer / 255, outer * 0.75 / 255), (inner / 255, inner * 0.75 / 255),
                      (inner / 255, inner * 0.75 / 255), (outer / 255, outer * 0.75 / 255)]
            glow = max(glow, inner)
        return st, glow > 128
    if t < 2500:
        u = t - 1900
        out = []
        for i in range(4):
            k = max(0, min(255, (u - i * 90) * 255 // 300))
            out.append(((255 - k) / 255, 1.0))
        return out, False
    u = t - 2500
    lvl = 1.0 if u < 500 else max(0.0, 1 - (u - 500) / 300)
    b = binary_leds(14, True)
    return [(r * lvl, g * lvl) for r, g in b], False


def wrap(text, fnt, width):
    words, lines, cur = text.split(), [], ""
    for w_ in words:
        test = (cur + " " + w_).strip()
        if fnt.getlength(test) > width and cur:
            lines.append(cur)
            cur = w_
        else:
            cur = test
    if cur:
        lines.append(cur)
    return lines


def static_layer(c, chapters):
    im = Image.new("RGB", (W, H), c["ground"])
    d = ImageDraw.Draw(im)
    dim = mix(INK, c["ground"], 0.38)
    rule = mix(INK, c["ground"], 0.75)
    d.text((96, 70), c["code"], font=F_CODE, fill=c["accent"])
    d.text((96 + F_CODE.getlength(c["code"]) + 22, 72), "cirrostratus · firmware for monsoon",
           font=F_MONO_S, fill=dim)
    d.text((90, 108), c["title"], font=F_TITLE, fill=INK)
    y = 232
    d.text((96, y), c["sub"], font=F_SUB, fill=dim)
    y += 52
    if c.get("forecast"):
        d.text((96, y), "forecast: " + c["forecast"].lower(), font=F_MONO, fill=c["accent"])
        y += 44
    for ln in wrap(c["desc"], F_BODY, 980)[:5]:
        d.text((96, y), ln, font=F_BODY, fill=INK)
        y += 38
    c["rows_y"] = max(y + 30, 520)
    d.line([(96, c["rows_y"] - 18), (1090, c["rows_y"] - 18)], fill=rule, width=1)
    # stdio tracklist (right column)
    tx, ty = 1290, 600
    d.text((tx, ty - 44), "tracklist", font=F_MONO_S, fill=dim)
    idx = c["index"]
    lo = max(0, min(idx - 3, len(chapters) - 8))
    for k, other in enumerate(chapters[lo:lo + 8]):
        yy = ty + k * 34
        cur = other["index"] == idx
        if cur:
            skew_rect(d, tx - 12, yy - 3, 520, 32, c["accent"], 0.5)
        col = c["ground"] if cur else (INK if other["index"] < idx else dim)
        d.text((tx, yy), "%s  %s" % (other["code"], other["title"]), font=F_MONO, fill=col)
    return im


def draw_rows(d, c, t, dur):
    rows = c["rows"][:8]
    if not rows:
        return
    y0 = c["rows_y"]
    gap = min(64, (980 - y0) // max(1, len(rows)))
    active = int((t / max(dur, 0.1)) * len(rows) * 1.0) % len(rows)
    dim = mix(INK, c["ground"], 0.45)
    for i, (label, text) in enumerate(rows):
        y = y0 + i * gap
        on = i == active
        col = INK if on else dim
        d.text((96, y), label.lower()[:22], font=F_MONO_M if on else F_MONO, fill=col)
        # slider: thin rule, slanted fill, 2 px white edge
        x0, x1 = 330, 560
        d.line([(x0, y + 15), (x1, y + 15)], fill=mix(INK, c["ground"], 0.65), width=1)
        v = 0.5 + 0.42 * math.sin(t * (0.6 + 0.13 * i) + i * 1.3)
        if on:
            v = 0.5 + 0.48 * math.sin(t * 1.7 + i)
        wv = (x1 - x0) * v
        skew_rect(d, x0, y + 6, wv, 18, c["accent"] if on else mix(c["accent"], c["ground"], 0.45))
        d.line([(x0 + wv + 9, y + 4), (x0 + wv, y + 25)], fill=INK, width=2)
        lines = wrap(text.lower(), F_MONO_S, 560)
        if len(lines) == 1:
            d.text((600, y + 1), lines[0], font=F_MONO, fill=col)
        else:
            d.text((600, y - 6), lines[0], font=F_MONO_S, fill=col)
            d.text((600, y + 17), (lines[1] + (" …" if len(lines) > 2 else ""))[:60], font=F_MONO_S, fill=col)


def render_chapter(args):
    c, chapters, dur, path = args
    base = static_layer(c, chapters)
    nframes = int(round(dur * FPS))
    ff = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                           "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-", "-c:v", "libx264",
                           "-preset", "medium", "-crf", "19", "-pix_fmt", "yuv420p", path],
                          stdin=subprocess.PIPE)
    for f in range(nframes):
        t = f / FPS
        im = base.copy()
        d = ImageDraw.Draw(im)
        # fade in/out
        draw_ico(d, t + c["index"] * 3.1, 1530, 300, 205, c["accent"], white_face=c["index"] % 20)
        draw_strips(d, 1585, 955, t, c["index"])
        draw_rows(d, c, t, dur)
        # LEDs
        if c["kind"] == "mode":
            n = c["num"]
            states = binary_leds((n - 1) % 15 + 1, n <= 15)
            led_row(d, 120, 1010, states, freeze=False)
        elif c["kind"] == "sunrise":
            ms = (t * 1000) % 4200
            states, frz = splash_leds(ms)
            led_row(d, 120, 1010, states, freeze=frz)
        elif c["kind"] == "menu":
            n = int(t / 1.2) % 30 + 1
            led_row(d, 120, 1010, binary_leds((n - 1) % 15 + 1, n <= 15))
        elif c["kind"] == "env":
            led_row(d, 120, 1010, [(0, 1), (0, 0), (0, 0.4), (1, 0)])
        elif c["kind"] == "steps":
            ph = int(t * 2) % 4
            led_row(d, 120, 1010, [(0, 1)] * 4 if ph == 3 else [(0, 0)] * 4, freeze=(int(t * 4) % 2 == 0))
        else:
            k = int(t * 3) % 4
            led_row(d, 120, 1010, [((0, 0.8) if i == k else (0, 0)) for i in range(4)])
        # chapter progress (skewed bar at the bottom)
        prog = (c["index"] + t / dur) / len(chapters)
        skew_rect(d, 0, H - 8, W * prog, 8, c["accent"], 0.5)
        # fades
        a = min(1.0, t / 0.45, (dur - t) / 0.45)
        if a < 1.0:
            im = Image.blend(Image.new("RGB", (W, H), (6, 10, 20)), im, max(0.0, a))
        ff.stdin.write(im.tobytes())
    ff.stdin.close()
    ff.wait()
    return path


# ---------------------------------------------------------------------------
# Audio.

def tts(text, path):
    aiff = path + ".aiff"
    subprocess.run(["say", "-v", "Samantha", "-r", "168", "-o", aiff, text], check=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", aiff, "-ar", str(SR), "-ac", "1", "-f", "f32le", path],
                   check=True)
    os.remove(aiff)
    return np.fromfile(path, dtype=np.float32)


def load_music(path, seconds):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-t", "%.3f" % seconds, "-ar", str(SR),
                          "-ac", "2", "-f", "f32le", "-"], check=True, stdout=subprocess.PIPE).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).copy()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--music", required=True)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "Cirrostratus-Video-Manual.mp4"))
    ap.add_argument("--only", help="chapter range a-b (for previews)")
    ap.add_argument("--work", default=None)
    ap.add_argument("--jobs", type=int, default=max(1, os.cpu_count() - 2))
    a = ap.parse_args()

    chapters = build_chapters()
    sel = chapters
    if a.only:
        lo, hi = (int(x) for x in a.only.split("-"))
        sel = chapters[lo:hi + 1]
    work = a.work or tempfile.mkdtemp(prefix="cirro_video_")
    os.makedirs(work, exist_ok=True)
    print("work dir", work, "-", len(sel), "chapters")

    # Narration first: it sets each chapter's length.
    voices, durs = [], []
    lead = 0.7
    for c in sel:
        v = tts(speakable(c["say"]), os.path.join(work, "%s.f32" % c["code"].replace("∞", "end")))
        voices.append(v)
        rows_time = len(c["rows"][:8]) * 2.2
        durs.append(max(lead + len(v) / SR + 1.6, rows_time, 7.0))
    total = sum(durs)
    print("total %.1f s" % total)

    jobs = [(c, chapters, d, os.path.join(work, "v%03d.mp4" % c["index"])) for c, d in zip(sel, durs)]
    with Pool(a.jobs) as pool:
        parts = pool.map(render_chapter, jobs)
    listing = os.path.join(work, "parts.txt")
    open(listing, "w").write("".join("file '%s'\n" % p for p in parts))
    video = os.path.join(work, "video.mp4")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", listing, "-c", "copy",
                    video], check=True)

    # Voice track + music ducked under it.
    n = int(total * SR) + SR
    voice = np.zeros(n, np.float32)
    pos = 0.0
    for v, d in zip(voices, durs):
        s = int((pos + lead) * SR)
        voice[s:s + len(v)] += v[:max(0, n - s)]
        pos += d
    music = load_music(os.path.expanduser(a.music), n / SR)
    if len(music) < n:
        music = np.concatenate([music, np.zeros((n - len(music), 2), np.float32)])
    music = music[:n]
    # Ducking: follow the voice envelope (40 ms attack, 600 ms release).
    env = np.abs(voice)
    att, rel = 1 - math.exp(-1 / (0.04 * SR)), 1 - math.exp(-1 / (0.6 * SR))
    e = np.zeros_like(env)
    acc = 0.0
    step = 64
    for i in range(0, n, step):
        x = env[i:i + step].max()
        acc = acc + ((att * step) if x > acc else (rel * step)) * (x - acc)
        acc = max(0.0, min(1.0, acc))
        e[i:i + step] = acc
    speaking = np.clip(e * 12, 0, 1)
    gain = 0.62 - 0.44 * speaking
    fade = np.ones(n, np.float32)
    fi, fo = int(2.5 * SR), int(5 * SR)
    fade[:fi] = np.linspace(0, 1, fi)
    fade[-fo:] = np.linspace(1, 0, fo)
    music *= (gain * fade)[:, None]
    mixd = music + voice[:, None] * 0.9
    peak = np.abs(mixd).max()
    if peak > 0.98:
        mixd *= 0.98 / peak
    audio = os.path.join(work, "audio.f32")
    mixd.astype(np.float32).tofile(audio)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", video, "-f", "f32le", "-ar", str(SR), "-ac", "2",
                    "-i", audio, "-c:v", "copy", "-c:a", "aac", "-b:a", "256k", "-shortest",
                    "-movflags", "+faststart", a.out], check=True)
    print(a.out)


if __name__ == "__main__":
    main()
