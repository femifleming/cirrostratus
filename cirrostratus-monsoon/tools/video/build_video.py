#!/usr/bin/env python3
"""Cirrostratus video manual, in the faltung look (deep-blue edition).

  python3 tools/video/build_video.py --music ~/Desktop/Njari/Bliss.wav \
      --out ~/Desktop/Cirrostratus-Video-Manual.mp4 [--only 0-3] [--work DIR]

Every chapter is one flat deep-blue field: a mono code above a lowercase
title, the text and faltung sliders on the left; a faltung drawing of the
Monsoon panel in the middle (every control where it sits on the real panel);
a turning wireframe icosahedron, a stdio tracklist and folded strips on the
right. While the narrator names a control, its row lights up and the same
control lights up on the panel, joined by a line.

Narration: Kokoro, British English "Emma" (tts_kokoro.py, run in
tools/video/.venv), the AERIA OS narrator. The music sits underneath,
ducked while the voice speaks (--music-level, 0.75 since 1.2: a little
quieter than the 1.1 videos).

Content comes from the manual (docs/MANUAL.md, docs/modes_*.md), so the
video always matches it. Needs Pillow, numpy, ffmpeg.
"""
import argparse, hashlib, json, math, os, re, subprocess, tempfile
from multiprocessing import Pool

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))
VENV_PY = os.path.join(HERE, ".venv", "bin", "python")
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


def hexrgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


F_SUB = font("Inter-Regular.ttf", 30)
F_BODY = font("Inter-Regular.ttf", 26)
F_MONO = font("IBMPlexMono-Regular.ttf", 22)
F_MONO_S = font("IBMPlexMono-Regular.ttf", 18)
F_MONO_M = font("IBMPlexMono-Medium.ttf", 23)
F_CODE = font("IBMPlexMono-Medium.ttf", 26)
F_PANEL = font("IBMPlexMono-Medium.ttf", 19)
F_PANEL_T = font("Inter-Regular.ttf", 46)


def title_font(text, width=900):
    for size in (96, 88, 80, 72, 64, 56):
        f = font("Inter-Medium.ttf", size)
        if f.getlength(text) <= width:
            return f
    return font("Inter-Medium.ttf", 52)


# ---------------------------------------------------------------------------
# Content.

def clean_md(s):
    s = re.sub(r"\*\*(.+?)\*\*", r"\1", s)
    s = re.sub(r"\*(.+?)\*", r"\1", s)
    s = re.sub(r"`(.+?)`", r"\1", s)
    # Manual section references ("(3.7)", ", 3.7)") mean nothing in a video.
    s = re.sub(r"\s*\((?:see )?\d\.\d\)", "", s)
    s = re.sub(r",\s*\d\.\d(?=\))", "", s)
    return s.strip()


def speakable(s):
    s = clean_md(s)
    s = s.replace("1970s", "nineteen-seventies").replace("°", " degrees").replace("…", ".")
    s = re.sub(r"(\d)\s*ms\b", r"\1 milliseconds", s)
    s = re.sub(r"(\d)\s*kHz\b", r"\1 kilohertz", s)
    s = re.sub(r"(\d)\s*Hz\b", r"\1 hertz", s)
    s = re.sub(r"(\d)\s*dB\b", r"\1 decibels", s)
    s = re.sub(r"(\d)\s*st\b", r"\1 semitones", s)
    s = re.sub(r"(\d)\s*s\b", r"\1 seconds", s)
    s = re.sub(r"(\d)\s*m\b", r"\1 metres", s)
    s = re.sub(r"(\d)\s*%", r"\1 percent", s)
    s = re.sub(r"(?<![\w])\+(\d)", r"plus \1", s)
    s = re.sub(r"(?<![\w])−(\d)", r"minus \1", s)
    s = re.sub(r"\s*dB/oct\b", " decibels per octave", s)
    s = re.sub(r"/s\b", " per second", s)
    s = s.replace(" = ", " is ").replace("~", "about ").replace("Δ", " delta").replace("..", ".")
    s = re.sub(r"(\d)\s*×", r"\1 times", s)
    s = re.sub(r"×\s*(\d)", r"times \1", s)
    for a, b in [("→", " to "), ("←", " to "), ("↔", " to "), ("↑", " up"), ("↓", " down"),
                 ("·", ","), ("±", "plus or minus "), ("≈", "about "),
                 ("V/OCT", "V per octave"), ("V/oct", "V per octave"), ("½", "half"),
                 ("¼", "a quarter"), ("¾", "three quarters"), ("e.g.", "for example"),
                 ("—", ","), ("–", " to "), ("/", " or "), ("dBFS", "dB"), ("IR", "I.R."),
                 ("LEDs", "lights"), ("LED", "light"), ("CV", "C.V."), ("FBK", "F.B.K."),
                 ("FDB", "feedback"), ("RVB", "R.V.B."), ("FDN", "F.D.N."), ("BAL", "B.A.L."), ("DISP", "display"),
                 ("TRIG", "trig"), ("FFT", "F.F.T."), ("SWD", "S.W.D."), ("DSP", "D.S.P."),
                 ("(", ", "), (")", ", "), ("+", " and ")]:
        s = s.replace(a, b)
    s = re.sub(r"\s+,", ",", s)
    s = re.sub(r",\s*,", ",", s)
    s = re.sub(r",\s*\.", ".", s)
    s = re.sub(r",\s*:", ":", s)
    s = re.sub(r"\.\s*\.", ".", s)
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
            forecast, para, rows = "", [], []
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


# Panel keys (see PANEL below) a row label points at.
SLIDERS = {"position": 0, "density": 1, "size": 2, "texture": 3}
KNOBS = {"wet": 0, "blend": 0, "bal": 1, "spread": 1, "stereo": 1, "feedback": 2, "fbk": 2, "reverb": 3, "rvb": 3}


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
        keys += ["tune", "voct"]
    if "gain" in l:
        keys += ["gain"]
    if "freeze" in l:
        keys += ["freeze", "fgate"]
    if "trig" in l:
        keys += ["trig"]
    if "disp" in l:
        keys += ["disp"]
    if "write" in l:
        keys += ["write"]
    if "option" in l:
        keys += ["disp", "freeze"]
    if "limiter" in l:
        keys += ["out_l", "out_r"]
    seen, out = set(), []
    for k in keys:
        if k not in seen:
            seen.add(k)
            out.append(k)
    return out


def bq_leds(q, pending=False):
    """Buffer page LEDs for quality q: 1-3 lit (48/24/16 kHz), green 16-bit,
    red 8-bit; LED 4 amber while the choice is pending."""
    lit = 1 + q % 3
    col = (1, 0) if q >= 3 else (0, 1)
    leds = [col if i < lit else (0, 0) for i in range(3)]
    leds.append((1, 0.63) if pending else (0, 0))
    return leds


def fft_leds(s, pending=False):
    """FFT page LEDs: s = 0 green bar (each mode's own size: 4 = 2048 as in
    SPECTRAL), 1-4 an amber bar of s LEDs (256 .. 2048). Pending = the bar
    the page blinks; drawn steady here."""
    if s == 0:
        return [(0, 1)] * 4
    return [((1, 0.63) if i < s else (0, 0)) for i in range(4)]


def row(label, text, spoken=None, hl=None):
    return dict(label=label, text=text, spoken=spoken, hl=hl if hl is not None else hl_for(label))


def general_chapters():
    return [
        dict(code="cs00", title="cirrostratus", sub="a thin, high sheet of cloud for the monsoon",
             desc="Alternative firmware for Jak Plugg's Monsoon, the twelve HP Clouds. Thirty modes in two "
                  "banks, a forty-eight kilohertz floating-point engine, and eight hands-on controls in every mode.",
             say="Cirrostratus. A thin, high sheet of cloud for the Monsoon. It's alternative firmware for "
                 "Jak Plugg's Monsoon: thirty modes in two banks, a forty-eight kilohertz floating-point engine, "
                 "and eight controls in every mode: the four sliders, and now the BAL, FBK and RVB knobs too. "
                 "Same panel, same jacks. Only the weather changes.",
             rows=[row("modes", "30 · two banks", hl=[]), row("engine", "48 kHz · 32-bit float", hl=[]),
                   row("slots", "4 jars of sky", hl=[]), row("look up", "always", hl=[])], kind="intro"),
        dict(code="nw152", title="new in 1.5.2", sub="a spring tank, a smooth smear",
             desc="Mode 1 is SPRING: a spring reverb tank, every echo a falling chirp. SPECTRAL's SMEAR is "
                  "smooth at long blurs: no more flutter, no more dropouts. And since 1.5.1 no clicks when "
                  "a block runs long.",
             say="New in one point five point two.",
             rows=[row("1 · spring", "a spring reverb tank",
                       "Mode one is back, as Spring: the tank from the back of a guitar amp. Every echo arrives "
                       "as a falling chirp, the boing and the drip. Texture sets how long the drip is, RVB is "
                       "the dwell, and TRIG kicks the tank.", hl=["disp", "leds", "sl3", "k3"]),
                   row("smear", "smooth, at any blur",
                       "Spectral's Smear option is smooth now. Under a long blur, the bins of one note used to "
                       "drift against each other, so the smear fluttered like a stutter and sometimes cancelled "
                       "itself out. Now they move as one.", hl=["sl1"]),
                   row("1.5.1", "no more pulses",
                       "And since one point five point one, a block that runs a little long no longer leaves a "
                       "click: the effect glides out and back in.", hl=["out_l", "out_r"]),
                   row("checked", "every mode, on the chip's twin",
                       "Every mode and option ran again on an emulated copy of the module's chip, checked "
                       "against a computer: all of them pass.", hl=["sl0", "sl1", "sl2", "sl3"])], kind="steps"),
        dict(code="in01", title="installing", sub="getting the cloud up there",
             desc="Rack off. Computer out into IN L. Hold FREEZE and power on: the FREEZE button blinks. "
                  "Play cirrostratus.wav from the start. Four green lights as each block is written; "
                  "a little rain when it's done.",
             say="Installing.",
             rows=[row("1", "rack off · computer out → IN L",
                       "Switch the rack off, and cable your computer's output into IN L, the input jack at "
                       "the bottom left.", hl=["in_l"]),
                   row("2", "hold FREEZE · power on",
                       "Hold the FREEZE button and power on. It blinks: the module is listening.",
                       hl=["freeze"]),
                   row("3", "volume ¾ · GAIN noon",
                       "Set the computer volume to about three quarters, and the GAIN knob to noon.",
                       hl=["gain"]),
                   row("4", "play cirrostratus.wav",
                       "Play cirrostratus dot wav from the start. It sounds like a singing modem. The four "
                       "lights dance with the level, and all turn green each time a block is written.",
                       hl=["leds"]),
                   row("red?", "press FREEZE, play again",
                       "If they blink red, press FREEZE and play it again. Nothing can break: the bootloader "
                       "is never touched.", hl=["freeze"])], kind="steps"),
        dict(code="pn02", title="the panel", sub="what every knob, slider and jack does",
             desc="Same panel, same jacks. Here is where everything is, and what Cirrostratus asks of it.",
             say="The panel. Let's go round it, top to bottom.",
             rows=[row("disp", "display · mode menu",
                       "Top left, DISP: tap it to change what the lights show, hold it for the mode menu.",
                       hl=["disp"]),
                   row("leds", "meter · mode number · pages",
                       "The four lights along the top: the input meter, the mode number, and every page.",
                       hl=["leds"]),
                   row("write (pencil)", "load · save · bank flip",
                       "Top right, the pencil button, WRITE: load and save, and in the menu it flips banks.",
                       hl=["write"]),
                   row("tune", "pitch · ±4 octaves",
                       "The big TUNE knob is PITCH, plus or minus four octaves, with gentle magnets on "
                       "every semitone. It adds to the V per octave jack.", hl=["tune", "voct"]),
                   row("gain", "input level",
                       "GAIN, under it, is the analog input level, as before.", hl=["gain"]),
                   row("the 4 sliders", "the four sliders · cv below",
                       "The four sliders are POSITION, DENSITY, SIZE and TEXTURE. Each mode gives them its own "
                       "job, and each has a C.V. jack in the row below.",
                       hl=["sl0", "sl1", "sl2", "sl3", "cv0", "cv1", "cv2", "cv3"]),
                   row("wet", "dry ↔ wet, equal power",
                       "WET blends dry and wet at equal power, in every mode.",
                       hl=["k0", "kj0"]),
                   row("bal", "a control of its own, per mode",
                       "BAL, FBK and RVB are three more controls for each mode. BAL is two-sided: noon "
                       "is the plain sound, and each side does something different.", hl=["k1", "kj1"]),
                   row("fbk", "a control of its own, per mode",
                       "FBK and RVB start at zero. Each mode's chapter says what they do there.",
                       hl=["k2", "kj2"]),
                   row("rvb", "a control of its own, per mode",
                       "Each of the four small knobs has a C.V. jack under its label.", hl=["k3", "kj3"]),
                   row("v/oct · trig", "pitch cv · a job per mode",
                       "Bottom rows. V per octave adds to TUNE. TRIG does something different in every mode.",
                       hl=["voct", "trig"]),
                   row("freeze", "button + gate",
                       "FREEZE, the pale button, has its own gate jack next to it. A high gate flips "
                       "whatever the button says.", hl=["freeze", "fgate"]),
                   row("in l r · out l r", "stereo audio",
                       "Stereo in on the left, stereo out on the right.",
                       hl=["in_l", "in_r", "out_l", "out_r"])], kind="panel"),
        dict(code="mn03", title="choosing a mode", sub="hold disp, move position, let go",
             desc="Hold DISP: the lights show the mode in binary, green for bank one, red for bank two. "
                  "Move POSITION to browse, tap WRITE to flip banks, tap FREEZE for the mode's options, "
                  "and let go of DISP.",
             say="Choosing a mode.",
             rows=[row("hold disp", "menu opens",
                       "Hold DISP. The four lights show the mode as a binary number: green for bank one, "
                       "modes one to fifteen, red for bank two, sixteen to thirty.", hl=["disp", "leds"]),
                   row("position", "browse the bank", "Move the POSITION slider to browse.",
                       hl=["sl0"]),
                   row("tap write", "other bank", "Tap WRITE to flip banks.", hl=["write"]),
                   row("tap freeze", "options", "Tap FREEZE to step through the mode's options.",
                       hl=["freeze"]),
                   row("let go", "fade in", "Let go of DISP, and the new weather fades in.",
                       hl=["disp"])], kind="menu"),
        dict(code="sl04", title="four jars of sky", sub="save slots and slot morph",
             desc="Hold WRITE to save, tap WRITE to load. DISP picks the slot. Each mode keeps what matters: "
                  "a buffer, a spectrum, a wavetable, a tape. On the load page, FREEZE arms a slot as a morph "
                  "source for SPECTRAL and CAPTURE.",
             say="Four jars of sky. Each mode keeps what matters to it: a recorded buffer, a frozen spectrum, "
                 "a wavetable, a tape loop.",
             rows=[row("hold write", "save", "Hold WRITE to open the save page.", hl=["write"]),
                   row("tap write", "load", "Tap WRITE for the load page.", hl=["write"]),
                   row("disp", "pick a slot", "DISP picks the slot, and WRITE does it.", hl=["disp", "leds"]),
                   row("freeze (load page)", "arm morph source",
                       "On the load page, tap FREEZE instead, and that slot becomes a morph source that "
                       "Spectral and Capture can blend toward.", hl=["freeze"])], kind="slots"),
        dict(code="ev05", title="weather that follows you", sub="the envelope page",
             desc="Hold FREEZE and tap DISP. Your input's loudness can push WET, BAL, FBK or RVB "
                  "up or down: off, plus a half, plus one, minus a half, minus one. In every mode.",
             say="Weather that follows you. Hold FREEZE and tap DISP to open the envelope page. Your input's "
                 "loudness can push any of the four small knobs up or down, whatever they do in the mode. "
                 "DISP moves along, FREEZE sets the amount, WRITE saves.",
             rows=[row("wet", "+1", "A wet that opens on loud notes."),
                   row("bal", "off", "BAL left alone."),
                   row("fbk", "+½", "FBK that swells a little."),
                   row("rvb", "−1", "And RVB that backs off while you play.")], kind="env"),
        dict(code="su06", title="sunrise", sub="what the lights do when it starts",
             desc="Ice crystals twinkle. Two amber halos pulse outward. The sun breaks through, amber to green. "
                  "Then the forecast: your current mode, in binary, fading out.",
             say="Sunrise. Every time you power up: ice crystals twinkle, two amber halos pulse outward from "
                 "the middle while the FREEZE button glows, the sun breaks through from amber to green, and "
                 "then the forecast, your current mode in binary, fades away. Any button skips it.",
             rows=[row("0.0 s", "ice crystals", hl=["leds"]), row("0.8 s", "halo", hl=["leds", "freeze"]),
                   row("1.9 s", "the sun breaks through", hl=["leds"]),
                   row("2.5 s", "the forecast", hl=["leds"])], kind="sunrise"),
        dict(code="os07", title="the output section", sub="hard stereo, mono bass",
             desc="Hard stereo: left is left, right is right, exactly what the mode makes. Below sixty "
                  "hertz the main out is summed to mono, on by default. An equal-power blend, and a "
                  "limiter that stays out of the way.",
             say="The output section.",
             rows=[row("wet", "equal-power blend", "WET blends dry and wet at equal power."),
                   row("hard stereo", "exactly what the mode makes",
                       "The output is hard stereo. There's no widener any more: modes that are wide, "
                       "are wide on their own.", hl=["out_l", "out_r"]),
                   row("mono bass", "< 60 Hz mono · on by default",
                       "Below sixty hertz the whole main out is summed to mono, so your sub stays in the "
                       "middle of the room.", hl=["out_l", "out_r"]),
                   row("freeze + write", "mono bass on / off",
                       "Hold FREEZE and tap WRITE to switch it. Two green lights in the middle mean on, "
                       "two amber lights at the sides mean off.", hl=["freeze", "write", "leds"]),
                   row("limiter", "−1 dBFS", "And a limiter that stays out of the way.")], kind="output"),
        dict(code="bq08", title="longer buffers", sub="buffer quality, for every mode that records",
             desc="Hold WRITE and tap DISP. FREEZE steps through 48, 24 and 16 kHz, in 16 or 8 bit. "
                  "The lower settings hold up to six times more sound in STRETCH, CAPTURE, TAPE, "
                  "BARBERPOLE and BLOOM.",
             say="Longer buffers. One master setting for every mode that records. "
                 "Trade a little fidelity, for up to six times more sound.",
             rows=[dict(row("write + disp", "open the buffer page",
                            "Hold WRITE and tap DISP, within a second, before the save page opens.",
                            hl=["write", "disp"]), leds=bq_leds(0)),
                   dict(row("freeze", "48 · 24 · 16 kHz",
                            "FREEZE steps through the settings. One, two or three lights: forty-eight, "
                            "twenty-four or sixteen kilohertz.", hl=["freeze", "leds"]), leds="cycle"),
                   dict(row("green", "16-bit · lower rates lose the top",
                            "Green is sixteen bit. The lower rates simply lose the very top end, like dusk "
                            "coming in.", hl=["leds"]), leds=bq_leds(2, True)),
                   dict(row("red", "8-bit · twice as long again",
                            "Red is eight bit: twice as long again, with a fine grain of noise under the "
                            "loud parts.", hl=["leds"]), leds=bq_leds(5, True)),
                   dict(row("write · disp", "apply · cancel",
                            "WRITE applies it, and the running mode's buffer starts again, empty. Or tap "
                            "DISP to leave without changing a thing.", hl=["write", "disp"]), leds=bq_leds(5)),
                   dict(row("tape mono", "1.2 s → 7.2 s",
                            "Tape runs slower, so in mono its echoes go from just over a second, to seven. "
                            "Stretch, up to five.", hl=["sl0", "sl1", "sl2", "sl3"]), leds=bq_leds(5)),
                   dict(row("the others", "capture, barberpole, bloom: up to 6×",
                            "Capture holds three seconds, Barberpole's loop grows to three, and Bloom's swells "
                            "get six times longer.", hl=["out_l", "out_r"]),
                        leds=bq_leds(5)),
                   dict(row("slots", "keep their quality",
                            "And a save slot remembers the quality it was recorded with.",
                            hl=["write", "leds"]), leds=bq_leds(5))], kind="buffer"),
        dict(code="ff09", title="fft size", sub="one resolution for every spectral mode",
             desc="Hold DISP and tap FREEZE. FREEZE steps through each mode's own size, then 256, 512, 1024 "
                  "and 2048 points, for SPECTRAL and SMEAR, CROSS, STRETCH, PUTZ, QUELL, SPECDELAY and MIRROR. "
                  "WRITE applies, DISP cancels.",
             say="F F T size. One setting for the resolution of every spectral mode.",
             rows=[dict(row("disp + freeze", "open the fft page",
                            "Hold DISP and tap FREEZE. A green light sweeps across: you're on the page.",
                            hl=["disp", "freeze"]), leds="sweep"),
                   dict(row("green bar", "each mode's own size",
                            "A green bar is the default: each mode at its own size, exactly as before.",
                            hl=["leds"]), leds=fft_leds(0)),
                   dict(row("freeze", "256 · 512 · 1024 · 2048",
                            "FREEZE steps through the sizes: an amber bar of one to four lights, two hundred "
                            "and fifty-six points up to two thousand and forty-eight.", hl=["freeze", "leds"]),
                        leds="cycle"),
                   dict(row("small", "sharp, grainy, quick",
                            "Small sizes are sharp and quick: transients survive, spectra turn grainy.",
                            hl=["leds"]), leds=fft_leds(1, True)),
                   dict(row("large", "smooth, glassy",
                            "Large sizes are smooth and glassy, with finer pitch.", hl=["leds"]),
                        leds=fft_leds(4, True)),
                   dict(row("write · disp", "apply · cancel",
                            "WRITE applies it, all four lights flash, and the running spectral mode restarts. "
                            "DISP leaves without a change. Blur, decay and delay times stay the same at every "
                            "size: only the resolution changes.", hl=["write", "disp"]), leds=fft_leds(4)),
                   dict(row("slots", "keep their size",
                            "A frozen spectrum saved in a slot comes back at the size it was saved with.",
                            hl=["write", "leds"]), leds=fft_leds(4))], kind="buffer"),
    ]


def build_chapters():
    ch = general_chapters()
    for m in parse_modes():
        code = ("cs" if m["num"] <= 15 else "cr") + "%02d" % m["num"]
        rows = []
        for label, text in m["rows"]:
            if label.lower().startswith("keeps"):
                continue
            spoken = "%s: %s." % (label.replace("(hold)", "held").replace("Options", "Options"), text)
            rows.append(row(label, text, spoken))
        say = "Mode %d. %s. %s. %s. %s" % (m["num"], m["name"].lower(), m["sub"], m["forecast"].rstrip("."), m["desc"])
        ch.append(dict(code=code, title=m["name"].lower(), sub=m["sub"].lower(), desc=m["desc"],
                       forecast=m["forecast"], say=say, rows=rows, kind="mode", num=m["num"]))
    ch.append(dict(code="cs∞", title="look up", sub="cirrostratus · firmware for monsoon",
                   desc="Thirty kinds of sky. Thanks to Mutable Instruments, Tom Burns and Jak Plugg.",
                   say="That's Cirrostratus. Thirty kinds of sky, in one small cloud. Thanks to Mutable Instruments, "
                       "Tom Burns and Jak Plugg. Now go and look up.",
                   rows=[row("modes", "30", hl=[]), row("tests", "231 passing", hl=[]),
                         row("licence", "mit", hl=[])], kind="outro"))
    for i, c in enumerate(ch):
        c["index"] = i
        c["ground"] = hexrgb(GROUNDS[i % len(GROUNDS)])
        c["accent"] = hexrgb(ACCENTS[i % len(ACCENTS)])
    return ch


def short_chapters():
    """The 90-second poem: the manual, condensed, for posting."""
    allk = ["sl0", "sl1", "sl2", "sl3"]
    ch = [
        dict(code="cs", title="cirrostratus", sub="a thin, high sheet of cloud",
             desc="Firmware for the Monsoon. Thirty kinds of weather, at forty-eight kilohertz, in floating point.",
             say="Cirrostratus. A thin, high sheet of cloud, drawn across the Monsoon. Thirty kinds of weather, "
                 "sung at forty-eight kilohertz. The same panel. A different sky.",
             rows=[row("modes", "30 · two banks", hl=[]), row("engine", "48 kHz · float", hl=[]),
                   row("look up", "always", hl=[])], kind="intro"),
        dict(code="pn", title="the panel", sub="four sliders, one big knob, three new hands",
             desc="POSITION, DENSITY, SIZE, TEXTURE shape the sky. TUNE is pitch. BAL, FBK and RVB belong to "
                  "each mode. WET blends.",
             say="Four sliders shape the sky: position, density, size and texture. Tune is pitch, and it leans "
                 "into every semitone. The three small knobs, BAL, FBK and RVB, become new hands in every mode. "
                 "And wet decides how much of the sky you let in.",
             rows=[row("sliders", "position · density · size · texture", hl=allk),
                   row("tune", "pitch, v/oct", hl=["tune", "voct"]),
                   row("bal fbk rvb", "new hands in every mode", hl=["k1", "k2", "k3"]),
                   row("wet", "how much sky", hl=["k0"])], kind="panel"),
        dict(code="mn", title="thirty skies", sub="hold disp, slide position, let go",
             desc="Hold DISP: the lights count in binary, green for the first bank, red for the second. "
                  "POSITION browses, WRITE crosses banks, FREEZE picks an option.",
             say="Hold display, and the four lights count in binary. Green is the first bank, red the second. "
                 "Slide position to wander, tap write to cross over, and let go.",
             rows=[row("hold disp", "the lights count", hl=["disp", "leds"]),
                   row("position", "wander", hl=["sl0"]), row("write", "cross the banks", hl=["write"]),
                   row("let go", "the new sky fades in", hl=["disp"])], kind="menu"),
        dict(code="wx", title="the weather", sub="a few of the thirty",
             desc="Spectra held and refracted. Strings with a buzzing bridge. "
                  "Tabla and tanpura. Plates of glass. A choir of you. Rooms that loop forever.",
             say="A spectrum, held and refracted into its colours. Strings with a "
                 "buzzing bridge, a tabla and its tanpura, plates of metal and glass, a choir made of you, "
                 "and rooms that loop back into themselves forever.",
             rows=[row("spectral", "held, refracted", hl=[]), row("resonator", "a buzzing bridge", hl=[]),
                   row("tantri", "tabla · tanpura", hl=[]), row("choir", "made of you", hl=[])], kind="menu"),
        dict(code="fz", title="hold the moment", sub="freeze, trig, four jars of sky",
             desc="FREEZE holds the moment. TRIG asks every mode a different question. WRITE keeps what you "
                  "caught in four jars of sky.",
             say="Freeze holds the moment still. Trig asks every mode a different question. And four jars of "
                 "sky keep whatever you caught: a buffer, a spectrum, a tape.",
             rows=[row("freeze", "the moment, held", hl=["freeze", "fgate"]),
                   row("trig", "a question per mode", hl=["trig"]),
                   row("write", "four jars of sky", hl=["write"])], kind="steps"),
        dict(code="os", title="the output", sub="hard stereo, a sub in the middle",
             desc="Left is left, right is right. Below sixty hertz the bass gathers in the middle. A limiter "
                  "that never argues.",
             say="Left stays left, and right stays right. Below sixty hertz, the bass gathers in the middle of "
                 "the room. And a limiter that never argues.",
             rows=[row("hard stereo", "left is left", hl=["out_l", "out_r"]),
                   row("mono bass", "< 60 Hz, centred", hl=["out_l", "out_r"]),
                   row("limiter", "−1 dBFS", hl=["out_l", "out_r"])], kind="output"),
        dict(code="su", title="sunrise", sub="every time you power on",
             desc="Ice crystals. Two amber halos. The sun breaking through to green. Then the forecast.",
             say="And every time you wake it: ice crystals, two amber halos, the sun breaking through to green, "
                 "and then the forecast. Cirrostratus. Look up.",
             rows=[row("ice", "crystals", hl=["leds"]), row("halo", "amber, twice", hl=["leds", "freeze"]),
                   row("sun", "through to green", hl=["leds"])], kind="sunrise"),
    ]
    for i, c in enumerate(ch):
        c["index"] = i
        c["ground"] = hexrgb(GROUNDS[i % len(GROUNDS)])
        c["accent"] = hexrgb(ACCENTS[i % len(ACCENTS)])
    return ch


def segments(c):
    """What is spoken, in order: (text, row index or None)."""
    segs = [(c["say"], None)]
    for i, r in enumerate(c["rows"]):
        if r["spoken"]:
            segs.append((r["spoken"], i))
    return segs


# ---------------------------------------------------------------------------
# The Monsoon panel, faltung style. Coordinates are in the units of a photo of
# the real panel (880 x 1818), so every control sits where it does on the
# module; PANEL_S scales it into the frame.

PANEL_X, PANEL_Y = 1060, 66
PANEL_S = 948 / 1818.0
PANEL_W, PANEL_H = 880, 1818

SL_X = [368, 511, 655, 800]
SL_TOP, SL_BOT = 285, 820
KN_X = [133, 338, 541, 745]
JR1 = [70, 218, 365, 512, 660, 808]
LED_X = [272, 384, 494, 604]

PANEL = {
    "disp": ("button", 145, 178, 36),
    "write": ("button", 735, 178, 36),
    "leds": ("leds", 438, 178, 190),
    "tune": ("knob", 135, 345, 78),
    "gain": ("knob", 135, 640, 68),
    "voct": ("jack", JR1[0], 1405, 40), "trig": ("jack", JR1[1], 1405, 40),
    "in_l": ("jack", 70, 1580, 40), "in_r": ("jack", 218, 1580, 40),
    "freeze": ("fbutton", 365, 1580, 42), "fgate": ("jack", 512, 1580, 40),
    "out_l": ("jack", 660, 1580, 40), "out_r": ("jack", 808, 1580, 40),
}
for i in range(4):
    PANEL["sl%d" % i] = ("slider", SL_X[i], (SL_TOP + SL_BOT) // 2, 30)
    PANEL["cv%d" % i] = ("jack", JR1[2 + i], 1405, 40)
    PANEL["k%d" % i] = ("sknob", KN_X[i], 1015, 46)
    PANEL["kj%d" % i] = ("jack", KN_X[i], 1255, 40)

KNOB_ANGLE = {"tune": 0, "gain": -130, "k0": -140, "k1": -140, "k2": -150, "k3": 30}


def P(x, y):
    return PANEL_X + x * PANEL_S, PANEL_Y + y * PANEL_S


def circ(d, x, y, r, **kw):
    cx, cy = P(x, y)
    rr = r * PANEL_S
    d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], **kw)


def ptext(d, x, y, s, fill, f=F_PANEL):
    cx, cy = P(x, y)
    d.text((cx - f.getlength(s) / 2, cy - f.size * 0.6), s, font=f, fill=fill)


def panel_static(d, c):
    g = c["ground"]
    face = mix(g, INK, 0.075)
    edge = mix(g, INK, 0.3)
    lab = mix(INK, face, 0.22)
    hole = mix(g, (0, 0, 0), 0.55)
    body = mix(g, (0, 0, 0), 0.35)
    x0, y0 = P(0, 0)
    x1, y1 = P(PANEL_W, PANEL_H)
    # one skewed shadow strip behind, then the flat face
    d.polygon([(x0 + 18, y0 + 18), (x1 + 18, y0 + 18), (x1 + 18, y1 + 18), (x0 + 18, y1 + 18)],
              fill=mix(g, (0, 0, 0), 0.3))
    d.rectangle([x0, y0, x1, y1], fill=face, outline=edge, width=2)
    for sx, sy in [(110, 45), (770, 45), (115, 1770), (770, 1770)]:
        circ(d, sx, sy, 16, fill=hole)
    ptext(d, 450, 62, "MONSOON", lab, F_PANEL_T)
    ptext(d, 62, 118, "DISP", lab)
    # pencil (WRITE)
    px, py = P(800, 120)
    d.line([(px - 6, py + 8), (px + 7, py - 8)], fill=lab, width=3)
    # sliders: slot, ticks, labels
    for i, x in enumerate(SL_X):
        ax, ay = P(x - 17, SL_TOP)
        bx, by = P(x + 17, SL_BOT)
        d.rounded_rectangle([ax, ay, bx, by], radius=6, fill=hole)
        ptext(d, x, 866, ["POSI", "DENS", "SIZE", "TEXT"][i], lab)
    for x in (SL_X[0], SL_X[3]):
        circ(d, x, 260, 13, outline=lab, width=2)
    lx0, ly = P(318, 553)
    lx1, _ = P(845, 553)
    d.line([(lx0, ly), (lx1, ly)], fill=mix(face, INK, 0.25), width=1)
    ptext(d, 145, 497, "TUNE", lab)
    ptext(d, 145, 780, "GAIN", lab)
    for k in ("tune", "gain"):
        _, x, y, r = PANEL[k]
        circ(d, x, y, r, fill=body)
    # small knobs, their label, a stem down to the cv jack
    for i, x in enumerate(KN_X):
        circ(d, x, 1015, 46, fill=body)
        a, b = P(x, 1065)
        _, e = P(x, 1120)
        d.line([(a, b), (a, e)], fill=lab, width=1)
        _, f = P(x, 1170)
        _, g2 = P(x, 1215)
        d.line([(a, f), (a, g2)], fill=lab, width=1)
        ptext(d, x, 1145, ["WET", "BAL", "FBK", "RVB"][i], lab)
    for k, (kind, x, y, r) in PANEL.items():
        if kind == "jack":
            circ(d, x, y, r + 6, outline=mix(face, INK, 0.45), width=2)
            circ(d, x, y, r * 0.55, fill=hole)
    for i, s in enumerate(["V/OCT", "TRIG", "POSI", "DENS", "SIZE", "TEXT"]):
        ptext(d, JR1[i], 1490, s, lab)
    ptext(d, 144, 1662, "L   IN   R", lab)
    ptext(d, 440, 1662, "FREEZE", lab)
    ptext(d, 734, 1662, "L   OUT   R", lab)
    for k in ("disp", "write"):
        _, x, y, r = PANEL[k]
        circ(d, x, y, r, fill=body)
    # a little faltung mark where the logo sits
    mx, my = P(440, 1760)
    d.polygon([(mx - 22, my + 12), (mx - 6, my - 14), (mx + 22, my - 14), (mx + 6, my + 12)],
              fill=mix(face, INK, 0.35))


def led_col(r, g, ground):
    if r < 0.01 and g < 0.01:
        return mix(ground, (0, 0, 0), 0.4)
    return (int(min(255, 40 + 215 * r)), int(min(255, 40 + 215 * g)), 50)


def panel_dynamic(d, c, t, hl, leds, freeze_on):
    g = c["ground"]
    acc = c["accent"]
    face = mix(g, INK, 0.075)
    ink = mix(INK, face, 0.05)
    pulse = 0.5 + 0.5 * math.sin(t * 5.0)
    # highlight halos first (under the controls)
    for k in hl:
        kind, x, y, r = PANEL[k]
        if kind == "slider":
            ax, ay = P(x - 30, SL_TOP - 14)
            bx, by = P(x + 30, SL_BOT + 14)
            d.rounded_rectangle([ax, ay, bx, by], radius=10, outline=acc, width=3)
        elif kind == "leds":
            ax, ay = P(LED_X[0] - 48, 178 - 46)
            bx, by = P(LED_X[3] + 48, 178 + 46)
            d.rounded_rectangle([ax, ay, bx, by], radius=12, outline=acc, width=3)
        else:
            circ(d, x, y, r + 18 + 8 * pulse, outline=acc, width=3)
    # sliders (caps move when they're being talked about)
    for i, x in enumerate(SL_X):
        on = ("sl%d" % i) in hl
        v = 0.5 + (0.38 * math.sin(t * 1.4 + i) if on else 0.0)
        cy = SL_BOT - (SL_BOT - SL_TOP) * v
        ax, ay = P(x - 26, cy - 34)
        bx, by = P(x + 26, cy + 34)
        lean = (by - ay) * 0.25
        col = acc if on else mix(face, INK, 0.55)
        d.polygon([(ax + lean, ay), (bx + lean, ay), (bx, by), (ax, by)], fill=col)
        mx, my = P(x, cy)
        d.line([(ax + 2, my), (bx - 2, my)], fill=INK if on else mix(face, (0, 0, 0), 0.3), width=2)
    # knobs: indicator lines (they turn when talked about)
    for k, base in KNOB_ANGLE.items():
        kind, x, y, r = PANEL[k]
        on = k in hl
        if on:
            circ(d, x, y, r, fill=mix(acc, g, 0.35))
        ang = math.radians(base + (70 * math.sin(t * 1.6) if on else 0))
        cx, cy = P(x, y)
        rr = r * PANEL_S
        d.line([(cx + math.sin(ang) * rr * 0.35, cy - math.cos(ang) * rr * 0.35),
                (cx + math.sin(ang) * rr * 0.9, cy - math.cos(ang) * rr * 0.9)], fill=ink, width=3)
    # jacks being talked about
    for k in hl:
        kind, x, y, r = PANEL[k]
        if kind == "jack":
            circ(d, x, y, r + 6, outline=acc, width=3)
    # buttons
    for k in ("disp", "write"):
        if k in hl:
            _, x, y, r = PANEL[k]
            circ(d, x, y, r, fill=mix(acc, g, 0.3))
    _, fx, fy, fr = PANEL["freeze"]
    circ(d, fx, fy, fr, fill=(250, 250, 246) if freeze_on else mix(face, INK, 0.38))
    # the four LEDs
    for i, (r, gg) in enumerate(leds):
        col = led_col(r, gg, g)
        circ(d, LED_X[i], 178, 24, fill=col)
        if r > 0.01 or gg > 0.01:
            circ(d, LED_X[i], 178, 36, outline=mix(col, g, 0.45), width=2)


# ---------------------------------------------------------------------------
# Other drawing.

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
            edges.add(tuple(sorted((f[i], f[(i + 1) % 3]))))
    for e in edges:
        z = (v[e[0]][2] + v[e[1]][2]) / 2
        c = mix(col, INK, 0.35) if z > 0 else mix(col, (0, 0, 0), 0.35)
        d.line([pts[e[0]], pts[e[1]]], fill=c, width=3 if z > 0 else 1)


def skew_rect(d, x, y, w, h, fill, lean=0.55):
    o = h * lean
    d.polygon([(x + o, y), (x + w + o, y), (x + w, y + h), (x, y + h)], fill=fill)


def draw_strips(d, ox, oy, t, code):
    """snd folded strips."""
    for i in range(6):
        c = hexrgb(STRIPS[(i + code) % len(STRIPS)])
        x = ox + i * 40
        y = oy + 16 * math.sin(t * 0.5 + i * 0.7)
        L = 95 - i * 8
        d.polygon([(x, y), (x + 34, y - 20), (x + 34, y - 20 + L), (x, y + L)], fill=c)
        d.polygon([(x + 34, y - 20), (x + 68, y), (x + 68, y + L), (x + 34, y - 20 + L)],
                  fill=mix(c, (0, 0, 0), 0.28))


def binary_leds(n, green):
    return [((0.0, 1.0) if green else (1.0, 0.0)) if (n >> (3 - i)) & 1 else (0.0, 0.0) for i in range(4)]


def splash_leds(ms):
    """Python port of Ui::PaintSplash (monsoon/ui.cc), for the sunrise chapter."""
    t = int(ms)

    def tri(tt, period):
        if tt < 0 or tt >= period:
            return 0
        h = period // 2
        return tt * 255 // h if tt < h else (period - tt) * 255 // h
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
        return [((255 - max(0, min(255, (u - i * 90) * 255 // 300))) / 255, 1.0) for i in range(4)], False
    u = t - 2500
    lvl = 1.0 if u < 500 else max(0.0, 1 - (u - 500) / 300)
    return [(r * lvl, g * lvl) for r, g in binary_leds(14, True)], False


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


TEXT_R = 960      # right edge of the text column
ROW_LX, ROW_SX0, ROW_SX1, ROW_TX = 96, 352, 462, 492


def static_layer(c, chapters):
    im = Image.new("RGB", (W, H), c["ground"])
    d = ImageDraw.Draw(im)
    dim = mix(INK, c["ground"], 0.38)
    rule = mix(INK, c["ground"], 0.75)
    d.text((96, 64), c["code"], font=F_CODE, fill=c["accent"])
    d.text((96 + F_CODE.getlength(c["code"]) + 22, 66), "cirrostratus · firmware for monsoon",
           font=F_MONO_S, fill=dim)
    ft = title_font(c["title"], TEXT_R - 96)
    d.text((90, 100), c["title"], font=ft, fill=INK)
    y = 220
    for ln in wrap(c["sub"], F_SUB, TEXT_R - 96)[:2]:
        d.text((96, y), ln, font=F_SUB, fill=dim)
        y += 40
    y += 8
    if c.get("forecast"):
        for ln in wrap("forecast: " + c["forecast"].lower(), F_MONO, TEXT_R - 96)[:2]:
            d.text((96, y), ln, font=F_MONO, fill=c["accent"])
            y += 32
        y += 10
    nrows = len(c["rows"])
    maxlines = 5 if nrows <= 6 else 4
    for ln in wrap(c["desc"], F_BODY, TEXT_R - 96)[:maxlines]:
        d.text((96, y), ln, font=F_BODY, fill=INK)
        y += 36
    c["rows_y"] = max(y + 34, 470 if nrows > 8 else 520)
    d.line([(96, c["rows_y"] - 18), (TEXT_R, c["rows_y"] - 18)], fill=rule, width=1)
    panel_static(d, c)
    # stdio tracklist (right column)
    tx, ty = 1600, 470
    d.text((tx, ty - 40), "tracklist", font=F_MONO_S, fill=dim)
    idx = c["index"]
    lo = max(0, min(idx - 3, len(chapters) - 9))
    for k, other in enumerate(chapters[lo:lo + 9]):
        yy = ty + k * 32
        cur = other["index"] == idx
        if cur:
            skew_rect(d, tx - 10, yy - 2, 285, 28, c["accent"], 0.5)
        col = c["ground"] if cur else (INK if other["index"] < idx else dim)
        d.text((tx, yy), "%s  %s" % (other["code"], other["title"][:18]), font=F_MONO_S, fill=col)
    return im


def row_geometry(c):
    n = len(c["rows"])
    y0 = c["rows_y"]
    gap = min(64, (1030 - y0) // max(1, n))
    return y0, gap


def draw_rows(d, c, t, active):
    rows = c["rows"]
    if not rows:
        return
    y0, gap = row_geometry(c)
    dim = mix(INK, c["ground"], 0.45)
    small = gap < 44
    for i, r in enumerate(rows):
        y = y0 + i * gap
        on = i == active
        col = INK if on else dim
        d.text((ROW_LX, y), r["label"].lower()[:19], font=F_MONO_M if on else F_MONO, fill=col)
        d.line([(ROW_SX0, y + 15), (ROW_SX1, y + 15)], fill=mix(INK, c["ground"], 0.65), width=1)
        v = 0.5 + 0.42 * math.sin(t * (0.6 + 0.13 * i) + i * 1.3)
        if on:
            v = 0.5 + 0.48 * math.sin(t * 1.7 + i)
        wv = (ROW_SX1 - ROW_SX0) * v
        skew_rect(d, ROW_SX0, y + 6, wv, 18, c["accent"] if on else mix(c["accent"], c["ground"], 0.45))
        d.line([(ROW_SX0 + wv + 9, y + 4), (ROW_SX0 + wv, y + 25)], fill=INK, width=2)
        if F_MONO.getlength(r["text"].lower()) <= TEXT_R - ROW_TX:
            d.text((ROW_TX, y + 1), r["text"].lower(), font=F_MONO, fill=col)
            continue
        lines = wrap(r["text"].lower(), F_MONO_S, TEXT_R - ROW_TX)
        if len(lines) == 1 or small:
            s = lines[0] + (" …" if len(lines) > 1 else "")
            d.text((ROW_TX, y + 1), s, font=F_MONO if len(lines) == 1 else F_MONO_S, fill=col)
        else:
            d.text((ROW_TX, y - 6), lines[0], font=F_MONO_S, fill=col)
            d.text((ROW_TX, y + 17), (lines[1] + (" …" if len(lines) > 2 else "")), font=F_MONO_S, fill=col)


def draw_connector(d, c, active, t):
    """A line from the spoken row to the control on the panel."""
    r = c["rows"][active]
    if not r["hl"]:
        return
    y0, gap = row_geometry(c)
    y = y0 + active * gap + 15
    kind, x, py, rad = PANEL[r["hl"][0]]
    tx, ty = P(x, py)
    if kind == "slider":
        ty = P(x, SL_TOP - 14)[1] if y < ty else P(x, SL_BOT + 14)[1]
    elif kind == "leds":
        tx, ty = P(LED_X[0] - 48, 178)
    else:
        tx -= (rad + 18) * PANEL_S
    acc = c["accent"]
    elbow = 1010
    d.line([(TEXT_R + 12, y), (elbow, y), (tx, ty)], fill=acc, width=2)
    d.ellipse([TEXT_R + 6, y - 6, TEXT_R + 18, y + 6], fill=acc)


def render_chapter(args):
    c, chapters, dur, timeline, path = args
    if os.path.exists(path) and os.path.getsize(path) > 0 and os.path.exists(path + ".done"):
        return path  # already rendered (re-runs only redo what's missing)
    base = static_layer(c, chapters)
    nframes = int(round(dur * FPS))
    ff = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                           "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-", "-c:v", "libx264",
                           "-preset", "medium", "-crf", "19", "-pix_fmt", "yuv420p", path],
                          stdin=subprocess.PIPE)
    nrows = len(c["rows"])
    for f in range(nframes):
        t = f / FPS
        im = base.copy()
        d = ImageDraw.Draw(im)
        draw_ico(d, t + c["index"] * 3.1, 1740, 225, 125, c["accent"], white_face=c["index"] % 20)
        draw_strips(d, 1600, 930, t, c["index"])
        # which row is being talked about
        active = None
        for start, ri in timeline:
            if t >= start and ri is not None:
                active = ri
        if active is None and nrows and not any(ri is not None for _, ri in timeline):
            active = int(t / max(dur, 0.1) * nrows) % nrows
        draw_rows(d, c, t, active)
        hl = c["rows"][active]["hl"] if active is not None else []
        # LEDs on the panel
        frz = False
        if c["kind"] == "mode":
            n = c["num"]
            leds = binary_leds((n - 1) % 15 + 1, n <= 15)
        elif c["kind"] == "sunrise":
            leds, frz = splash_leds((t * 1000) % 4200)
        elif c["kind"] == "menu":
            n = int(t / 1.2) % 30 + 1  # 1..30
            leds = binary_leds((n - 1) % 15 + 1, n <= 15)
        elif c["kind"] == "buffer":
            r = c["rows"][active] if active is not None else c["rows"][0]
            leds = r.get("leds", bq_leds(0))
            fft = c["code"].startswith("ff")
            if leds == "cycle":
                leds = fft_leds(1 + int(t / 0.9) % 4) if fft else bq_leds(int(t / 0.9) % 6)
            elif leds == "sweep":
                # green sweep from LED 4 to LED 1, then the own-size bar
                k = int(t / 0.15) % 6
                leds = [((0, 1) if 3 - i == k else (0, 0)) for i in range(4)] if k < 4 else fft_leds(0)
        elif c["kind"] == "env":
            leds = [(0, 1), (0, 0), (0, 0.4), (1, 0)]
        elif c["kind"] == "steps":
            ph = int(t * 2) % 4
            leds = [(0, 1)] * 4 if ph == 3 else [(0, 0)] * 4
            frz = int(t * 4) % 2 == 0
        else:
            k = int(t * 3) % 4
            leds = [((0, 0.8) if i == k else (0, 0)) for i in range(4)]
        panel_dynamic(d, c, t, hl, leds, frz)
        if active is not None:
            draw_connector(d, c, active, t)
        prog = (c["index"] + t / dur) / len(chapters)
        skew_rect(d, 0, H - 8, W * prog, 8, c["accent"], 0.5)
        a = min(1.0, t / 0.45, (dur - t) / 0.45)
        if a < 1.0:
            im = Image.blend(Image.new("RGB", (W, H), (6, 10, 20)), im, max(0.0, a))
        ff.stdin.write(im.tobytes())
    ff.stdin.close()
    ff.wait()
    open(path + ".done", "w").write("%.3f" % dur)
    return path


# ---------------------------------------------------------------------------
# Audio.

def speak_all(texts, work):
    """Kokoro (Emma) for every text, cached by content. Returns 48 kHz arrays."""
    paths = []
    jobs = []
    for s in texts:
        h = hashlib.sha1(("bf_emma|" + s).encode()).hexdigest()[:16]
        p = os.path.join(work, "tts_%s.wav" % h)
        paths.append(p)
        jobs.append(dict(text=s, out=p))
    jf = os.path.join(work, "tts_jobs.json")
    json.dump(jobs, open(jf, "w"))
    subprocess.run([VENV_PY, os.path.join(HERE, "tts_kokoro.py"), jf], check=True)
    out = []
    for p in paths:
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", p, "-ar", str(SR), "-ac", "1", "-f", "f32le", "-"],
                             check=True, stdout=subprocess.PIPE).stdout
        out.append(np.frombuffer(raw, np.float32).copy())
    return out


def load_music(path, seconds):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-t", "%.3f" % seconds, "-ar", str(SR),
                          "-ac", "2", "-f", "f32le", "-"], check=True, stdout=subprocess.PIPE).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).copy()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--music", required=True)
    ap.add_argument("--out", default=os.path.expanduser("~/Desktop/Cirrostratus-Video-Manual.mp4"))
    ap.add_argument("--only", help="chapter range a-b (for previews)")
    ap.add_argument("--work", default=None)
    ap.add_argument("--jobs", type=int, default=max(1, os.cpu_count() - 2))
    ap.add_argument("--still", help="write one frame per chapter as PNG at this time (s) and stop")
    ap.add_argument("--print", action="store_true", help="print the spoken text and stop")
    ap.add_argument("--short", action="store_true", help="the 90-second poem instead of the full manual")
    ap.add_argument("--music-level", type=float, default=0.75,
                    help="music bed level (1.0 = the 1.1 videos; 0.75 is about 2.5 dB quieter)")
    a = ap.parse_args()

    chapters = short_chapters() if a.short else build_chapters()
    sel = chapters
    if a.only:
        lo, hi = (int(x) for x in a.only.split("-"))
        sel = chapters[lo:hi + 1]
    if a.print:
        for c in sel:
            for s, ri in segments(c):
                print("[%s/%s] %s" % (c["code"], ri, speakable(s)))
        return
    work = os.path.abspath(a.work or tempfile.mkdtemp(prefix="cirro_video_"))
    os.makedirs(work, exist_ok=True)
    print("work dir", work, "-", len(sel), "chapters")

    # Narration first: it sets each chapter's length and when each row lights up.
    lead, gap, tail = 0.7, 0.45, 1.5
    all_segs = [(ci, ri, speakable(s)) for ci, c in enumerate(sel) for s, ri in segments(c)]
    voices = speak_all([s for _, _, s in all_segs], work)
    durs, timelines, placed = [], [], []
    pos = 0.0
    for ci, c in enumerate(sel):
        t = lead
        tl = []
        for (cj, ri, _), v in zip(all_segs, voices):
            if cj != ci:
                continue
            tl.append((t, ri))
            placed.append((pos + t, v))
            t += len(v) / SR + gap
        d = max(t - gap + tail, 7.0)
        if not any(ri is not None for _, ri in tl):
            d = max(d, len(c["rows"]) * 2.2)
        durs.append(d)
        timelines.append(tl)
        pos += d
    total = pos
    print("total %.1f s" % total)

    if a.still:
        ts = float(a.still)
        for c, d, tl in zip(sel, durs, timelines):
            p = os.path.join(work, "still_%03d" % c["index"])
            render_still(c, chapters, d, tl, min(ts, d - 0.6), p + ".png")
        return

    jobs = [(c, chapters, d, tl, os.path.join(work, "v%03d.mp4" % c["index"]))
            for c, d, tl in zip(sel, durs, timelines)]
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
    for start, v in placed:
        s = int(start * SR)
        voice[s:s + len(v)] += v[:max(0, n - s)]
    music = load_music(os.path.expanduser(a.music), n / SR)
    if len(music) < n:
        reps = int(math.ceil(n / max(1, len(music))))
        music = np.concatenate([music] * reps)
    music = music[:n]
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
    gain = (0.62 - 0.44 * speaking) * a.music_level
    fade = np.ones(n, np.float32)
    fi, fo = int(2.5 * SR), int(5 * SR)
    fade[:fi] = np.linspace(0, 1, fi)
    fade[-fo:] = np.linspace(1, 0, fo)
    music *= (gain * fade)[:, None]
    vpk = np.abs(voice).max()
    if vpk > 0:
        voice *= 0.8 / vpk
    mixd = music + voice[:, None]
    peak = np.abs(mixd).max()
    if peak > 0.98:
        mixd *= 0.98 / peak
    audio = os.path.join(work, "audio.f32")
    mixd.astype(np.float32).tofile(audio)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", video, "-f", "f32le", "-ar", str(SR), "-ac", "2",
                    "-i", audio, "-c:v", "copy", "-c:a", "aac", "-b:a", "256k", "-shortest",
                    "-movflags", "+faststart", os.path.expanduser(a.out)], check=True)
    print(a.out)


def render_still(c, chapters, dur, timeline, t, png):
    """One frame, for checking the layout."""
    import types
    frames = []

    class Cap:
        def __init__(self):
            self.stdin = types.SimpleNamespace(write=lambda b: frames.append(b), close=lambda: None)

        def wait(self):
            pass
    real = subprocess.Popen
    subprocess.Popen = lambda *a, **k: Cap()
    try:
        tmp = png + ".mp4"
        global FPS
        fps0 = FPS
        FPS = 1.0 / max(t, 0.01)
        render_chapter((c, chapters, max(dur, t + 0.6), timeline, tmp))
        FPS = fps0
        if os.path.exists(tmp + ".done"):
            os.remove(tmp + ".done")
    finally:
        subprocess.Popen = real
    Image.frombytes("RGB", (W, H), frames[min(1, len(frames) - 1)]).save(png)
    print(png)


if __name__ == "__main__":
    main()
