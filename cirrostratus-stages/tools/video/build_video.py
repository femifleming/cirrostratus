#!/usr/bin/env python3
"""The Cirrostratus Stages video manual: narrated chapters, each step showing the
Stages panel (tools/manual/panel.py, drawn from the photo of the module) with
the controls it talks about lit, next to the chapter's steps. The same look
and narrator as Cirrostratus' Monsoon video (deep blue, Inter / Plex Mono,
Kokoro "bf_emma"), music under the voice, ducked while she speaks.

  python3 tools/video/build_video.py --music ~/Desktop/Njari/Bliss.wav
      [--out ~/Desktop/Cirrostratus-Stages-Video-Manual-1.3.1.mp4] [--only 3-5]
      [--still] [--print]

The narration uses the Monsoon video's Kokoro set-up next door
(../cirrostratus-monsoon/tools/video/.venv and tts_kokoro.py). Speech is
cached by text in the work folder, so a re-run only speaks what changed.
The video itself never goes in the repository.
"""
import argparse, hashlib, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "manual"))
import panel  # noqa: E402

MONSOON_VIDEO = os.path.join(os.path.dirname(ROOT), "cirrostratus-monsoon", "tools", "video")
VENV_PY = os.path.join(MONSOON_VIDEO, ".venv", "bin", "python")
TTS = os.path.join(MONSOON_VIDEO, "tts_kokoro.py")
FONTS = os.path.expanduser("~/faltung-kit/assets/fonts")

SR = 48000
FPS = 30
W, H = 1920, 1080
GROUND = (15, 26, 46)
GROUND2 = (19, 32, 58)
INK = (246, 246, 240)
DIM = (130, 146, 172)
ACCENT = (149, 198, 219)
STRIP = (59, 95, 150)
FADE = 0.35


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


F_TITLE = font("Inter-Regular.ttf", 70)
F_SUB = font("Inter-Regular.ttf", 32)
F_LABEL = font("IBMPlexMono-Medium.ttf", 25)
F_ROW = font("Inter-Regular.ttf", 25)
F_CAP = font("Inter-Regular.ttf", 29)
F_SMALL = font("IBMPlexMono-Regular.ttf", 20)

G, Y, R = "green", "yellow", "red"
TYPE_LED = {"random": G, "chaos": Y, "envelope": R, "oscillator": "greenred",
            "processor": "yellowred", "effect": "greenyellow"}


# ---------------------------------------------------------------------------
# Speech.

def speakable(s):
    s = re.sub(r"\*\*(.+?)\*\*", r"\1", s)
    s = re.sub(r"(\d)\s*ms\b", r"\1 milliseconds", s)
    s = re.sub(r"(\d)\s*kHz\b", r"\1 kilohertz", s)
    s = re.sub(r"(\d)\s*Hz\b", r"\1 hertz", s)
    s = re.sub(r"(\d)\s*s\b", r"\1 seconds", s)
    s = re.sub(r"(\d)\s*V\b", r"\1 volts", s)
    s = re.sub(r"(\d)\s*%", r"\1 percent", s)
    s = re.sub(r"(?<![\w])\+(\d)", r"plus \1", s)
    s = re.sub(r"(?<![\w])[−-](\d)", r"minus \1", s)
    s = re.sub(r"(\d)\s*×", r"\1 times", s)
    s = re.sub(r"×\s*(\d)", r"times \1", s)
    for a, b in [("→", " to "), ("↔", " to "), ("·", ","), ("±", "plus or minus "),
                 ("V/oct", "volt per octave"), ("½", "half"), ("—", ","), ("–", " to "),
                 ("1/f", "one over f"), ("CV", "C.V."), ("VCA", "V.C.A."), ("FM", "F.M."),
                 ("LED", "light"), ("(", ", "), (")", ", "), ("/", " or ")]:
        s = s.replace(a, b)
    s = re.sub(r"\s+,", ",", s)
    s = re.sub(r",\s*([,.:])", r"\1", s)
    s = re.sub(r"\s+", " ", s)
    return s.strip()


def speak_all(texts, work):
    jobs, paths = [], []
    for t in texts:
        s = speakable(t)
        h = hashlib.sha1(s.encode()).hexdigest()[:16]
        p = os.path.join(work, "tts_%s.wav" % h)
        paths.append(p)
        jobs.append(dict(text=s, out=p))
    jf = os.path.join(work, "tts_jobs.json")
    json.dump(jobs, open(jf, "w"))
    subprocess.run([VENV_PY, TTS, jf], check=True)
    out = []
    for p in paths:
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", p, "-ar", str(SR), "-ac", "1", "-f", "f32le", "-"],
                             check=True, stdout=subprocess.PIPE).stdout
        out.append(np.frombuffer(raw, np.float32).copy())
    return out


# ---------------------------------------------------------------------------
# The script. Each chapter: title, subtitle, what she says over the title,
# a base panel (type lights per segment), and steps:
#   (label, on screen, spoken, figure)   figure = dict(rings, badges, dim, cols)

def cols(*leds, **kw):
    out = []
    for i, l in enumerate(leds):
        c = dict(led=l)
        c.update(kw.get("c%d" % i, {}))
        out.append(c)
    return out


def one(col, led, parts, extra=None):
    """A figure about one segment: its parts ringed and badged, the rest dim."""
    cs = [dict(led=None) for _ in range(6)]
    cs[col] = dict(led=led, glow=0.7, slider=0.55, pot=0.45)
    if extra:
        cs[col].update(extra)
    return dict(cols=cs, rings=[(col, p) for p, _ in parts],
                badges=[(col, p, b) for p, b in parts if b], dim=[i for i in range(6) if i != col])


DEFAULT = cols(G, G, G, G, R, "greenred")


def chapters():
    ch = []

    ch.append(dict(code="intro", title="Cirrostratus Stages", sub="every segment its own instrument, for Stages",
        say="Cirrostratus Stages. Every segment its own instrument, for Mutable Instruments Stages. It's a new mode "
            "on button six, part of Cirrostratus, built on BRIBESTAGES, and everything Stages and BRIBESTAGES "
            "do still works in their own modes.",
        steps=[
            ("six types", "tap a button: the next type",
             "In Cirrostratus Stages every one of the six segments is its own instrument. Tap a segment's button and it "
             "becomes the next type: random, chaos, envelope, oscillator, processor, or effect.",
             dict(cols=cols(G, Y, R, "greenred", "yellowred", "greenyellow"),
                  badges=[(0, "led", "random"), (1, "led", "chaos"), (2, "led", "env"),
                          (3, "led", "osc"), (4, "led", "proc"), (5, "led", "fx")])),
            ("flavours", "several of each, remembered",
             "Every type has several flavours, and every segment remembers its own flavour, settings and options "
             "for each type.", dict(cols=DEFAULT, rings=[(i, "slider") for i in range(6)])),
            ("cords and dreams", "neighbours, and a panel that drifts",
             "Segments can take each other's outputs through cords, and the whole panel can dream when you leave "
             "it alone.", dict(cols=DEFAULT, rings=[(0, "cv"), (1, "cv"), (2, "cv")])),
        ]))

    ch.append(dict(code="new", title="new in 1.3.1", sub="room to breathe, and a load meter",
        say="New in one point three point one.",
        steps=[
            ("buffer", "three blocks ahead",
             "The audio now runs up to three blocks ahead of the outputs, under a millisecond, so one slow block "
             "borrows time from the others instead of being missed.", dict(cols=DEFAULT,
                 rings=[(i, "out") for i in range(6)])),
            ("load meter", "hold 5 and 6 for a second",
             "Hold buttons five and six together for a second, and the slider lights show how much of the chip's "
             "time the engine uses, read from the chip's own cycle counter. Each light is a sixth. All six full "
             "means it's out of time.",
             dict(cols=cols(G, G, G, G, R, "greenred", c0=dict(glow=1.0), c1=dict(glow=1.0), c2=dict(glow=0.5)),
                  rings=[(4, "button"), (5, "button")] + [(i, "slider") for i in range(3)],
                  badges=[(4, "button", "hold"), (5, "button", "hold")])),
            ("overload light", "red flicker = out of time",
             "If the engine ever misses its deadline, all six button lights flicker red for half a second. Not "
             "when you change a type: that's the settings being saved to flash, which pauses the chip for a "
             "moment, just as on stock Stages.",
             dict(cols=cols(R, R, R, R, R, R), rings=[(i, "led") for i in range(6)])),
            ("1.3", "cleaner oscillators",
             "And since one point three, the oscillators are clean right across the slider: the wave bank and "
             "Morph keep everything that isn't the note sixty to seventy decibels below it.",
             one(5, "greenred", [("slider", "pitch"), ("pot", "timbre")])),
        ]))

    ch.append(dict(code="install", title="installing", sub="the update is an audio file",
        say="Installing. The module updates from an audio file. The updater is built in, and a failed try "
            "can't damage it, so you can always try again.",
        steps=[
            ("the file", "cirrostratus-stages-v1.3.1.wav",
             "You need a computer or phone with a headphone jack, a patch cable, and the file Cirrostratus Stages "
             "version one point three point one, from the Cirrostratus releases page. There's a flipped one for "
             "a module mounted upside down.", dict(cols=[dict(led=None) for _ in range(6)])),
            ("cv 6", "headphones into CV 6, slider 6 to the middle",
             "Power the rack off and unplug every cable. Patch the headphone out into the C.V. input of segment "
             "six, and set slider six to the middle: it's the input gain.",
             one(5, None, [("cv", "audio in"), ("slider", "level")], dict(slider=0.5))),
            ("hold 6", "hold button 6 and power on",
             "Hold button six and power the rack on. Slider six's light blinks green: the module is waiting.",
             one(5, None, [("button", "hold")], dict(glow=1.0, slider=0.5))),
            ("play", "3 or 4 lights lit is right",
             "Set the volume high, turn off sounds and notifications, and play the file from the start. The six "
             "button lights are a level meter: three or four lit is right. Move slider six to get there.",
             dict(cols=cols(G, G, G, G, None, None, c5=dict(slider=0.5)), rings=[(5, "slider")])),
            ("done", "about 80 seconds",
             "Green slider lights run left to right while it loads, about eighty seconds. When the file ends, "
             "the module restarts on the new firmware. If all six lights blink red, press button six, raise "
             "slider six, and play it again.",
             dict(cols=cols(None, None, None, None, None, None, **{"c%d" % i: dict(glow=1.0) for i in range(4)}))),
        ]))

    ch.append(dict(code="enter", title="getting in", sub="hold button 6 for 5 seconds",
        say="Getting into Cirrostratus Stages.",
        steps=[
            ("hold 6", "5 seconds",
             "Hold button six for five seconds. The lights show the mode, and the module starts in Cirrostratus Stages. "
             "It remembers the mode through power cycles.", one(5, R, [("button", "hold 5 s")])),
            ("first time", "1–4 random, 5 envelope, 6 oscillator",
             "The first time, the segments are what you probably want anyway: one to four random, five an "
             "envelope, six an oscillator.", dict(cols=DEFAULT,
                 badges=[(0, "led", "random"), (4, "led", "env"), (5, "led", "osc")])),
            ("leaving", "hold another button 5 s",
             "To leave, hold another button for five seconds, as in every Stages mode: one is Stages, two "
             "Advanced, three slow L.F.O., four six E.G., five Ouroboros. Hold five again in Ouroboros for its "
             "alternate.", dict(cols=DEFAULT, rings=[(i, "button") for i in range(5)])),
        ]))

    ch.append(dict(code="controls", title="one segment", sub="every control, whatever the type",
        say="One segment, every control. Each segment works the same way, whatever its type.",
        steps=[
            ("slider", "the main control",
             "The slider, plus its C.V. jack and any cord, is the main control: the rate, the pitch, the attack.",
             one(0, G, [("slider", "slider"), ("cv", "CV")])),
            ("knob", "the second control",
             "The knob is the second control: slew, timbre, decay.", one(0, G, [("pot", "knob")])),
            ("gate and out", "clock, trigger, sync · the output",
             "The gate jack is a clock, a trigger, a sync, a freeze or a record, depending on the type. The out "
             "jack is the segment's output.", one(0, G, [("gate", "gate"), ("out", "out")])),
            ("lights", "type colour · output level",
             "The button light shows the type by its colour, flashes on every tick or hit, and breathes when the "
             "extra option is on. The slider light is the output level.",
             one(0, G, [("led", "type"), ("slider", "level")], dict(glow=1.0))),
            ("tap", "next type",
             "Tap the button for the next type.", one(0, G, [("button", "tap")])),
            ("hold + slider", "the flavour",
             "Hold the button and move the slider to choose the flavour: the slider is cut into as many zones as "
             "the type has flavours, the bottom is one.", one(0, G, [("button", "hold"), ("slider", "flavour")])),
            ("hold + knob", "the hidden setting",
             "Hold the button and turn the knob to set the type's hidden setting: amplitude, curve, fine tune or "
             "gain. The slider light shows its value while you turn.",
             one(0, G, [("button", "hold"), ("pot", "hidden")])),
            ("hold ½ s", "the extra option",
             "Hold for half a second to five seconds and let go to toggle the type's extra option: bipolar, "
             "loop, voice or invert.", one(0, G, [("button", "½ s")])),
            ("frozen", "sliders and knobs lock while held",
             "While you hold a button, its slider and knob are frozen, so moving them sets things without "
             "changing the sound. When you let go they glide back. Settings are saved when you let go.",
             one(0, G, [("slider", None), ("pot", None)])),
        ]))

    ch.append(dict(code="flavour", title="reading the flavour", sub="the light while you hold",
        say="Reading the flavour. While you hold a button, its light shows the segment's flavour.",
        steps=[
            ("1 2 3", "green · yellow · red",
             "Flavour one is green, two yellow, three red.",
             dict(cols=cols(G, Y, R, None, None, None), badges=[(0, "led", "1"), (1, "led", "2"), (2, "led", "3")],
                  dim=[3, 4, 5])),
            ("4 5 6", "the same, blinking",
             "Four, five and six are the same colours, blinking.",
             dict(cols=[dict(led=None)] * 3 + [dict(led=G, blink=True), dict(led=Y, blink=True), dict(led=R, blink=True)],
                  badges=[(3, "led", "4"), (4, "led", "5"), (5, "led", "6")], dim=[0, 1, 2])),
            ("7 8", "green and red · yellow and red",
             "Seven alternates green and red, eight yellow and red.",
             dict(cols=cols(None, None, None, None, "greenred", "yellowred"),
                  badges=[(4, "led", "7"), (5, "led", "8")], dim=[0, 1, 2, 3])),
        ]))

    ch.append(dict(code="random", title="random", sub="green · slewed random voltages",
        say="Random. Slewed random voltages, one per segment, each with its own kind of randomness.",
        steps=[
            ("slider", "rate · 0.05 Hz to 100 Hz",
             "The slider is the rate: a twentieth of a hertz at the bottom, about two hertz at noon, a hundred "
             "at the top.", one(0, G, [("slider", "rate")])),
            ("gate", "clock · slider = chance",
             "With a cable in the gate jack, a new value comes on every clock, and the slider becomes the chance "
             "of a new value. At the bottom the value is locked.", one(0, G, [("gate", "clock"), ("slider", "chance")])),
            ("knob", "slew",
             "The knob is the slew: fully left, clean steps; turning right, each new value glides in.",
             one(0, G, [("pot", "slew")])),
            ("options", "amplitude · bipolar",
             "Hold and turn the knob for the amplitude. The extra option makes it bipolar, minus five to plus "
             "five volts instead of zero to eight.", one(0, G, [("button", "½ s"), ("pot", "amp")])),
            ("1–4", "uniform · gaussian · walk · pink",
             "The flavours. Uniform: any value. Gaussian: mostly near the middle. Walk: a random walk between two "
             "walls, after Xenakis. Pink: one over f drift, slow wandering with occasional leaps.",
             dict(cols=cols(G, G, G, G, None, None),
                  badges=[(i, "led", str(i + 1)) for i in range(4)], dim=[4, 5])),
            ("5–6", "quantised · kin",
             "Quantised: whole semitones at one volt per octave, over one to five octaves. Kin: stays close to "
             "the segment on its left, loosely.", one(1, G, [("led", "5–6")])),
            ("7 orbit", "X, Y, distance of one point",
             "Orbit: a point moving along a path the knob morphs, from a circle to a figure eight, a spiral, "
             "wandering. Side by side, orbit segments read the same point: X, Y, and its distance from the "
             "centre.", dict(cols=cols(G, G, G, None, None, None),
                             badges=[(0, "out", "X"), (1, "out", "Y"), (2, "out", "dist")], dim=[3, 4, 5])),
            ("8 weather", "sky · wind · rain · humidity · pressure",
             "Weather: a sky. Pressure fronts come and go, low pressure brings gusts of wind, and rain when the "
             "air is damp. Side by side, weather segments read one sky: the sky, the wind, the rain, the "
             "humidity, the pressure.",
             dict(cols=cols(G, G, G, G, G, None),
                  badges=[(0, "out", "sky"), (1, "out", "wind"), (2, "out", "rain"), (3, "out", "hum"),
                          (4, "out", "press")], dim=[5])),
        ]))

    ch.append(dict(code="chaos", title="chaos", sub="yellow · randomness with a shape",
        say="Chaos. Randomness with a shape you can lock, push and listen to. The slider is the rate, or the "
            "gate jack a clock, and the knob depends on the flavour.",
        steps=[
            ("1 turing", "lock · mutate · flip",
             "Turing loop: an eight-step loop of bits. Fully left it's locked, in the middle random, fully right "
             "it plays its eight steps and then their opposite.", one(0, Y, [("pot", "lock")])),
            ("2 logistic", "order to chaos",
             "Logistic map: one knob from order to chaos. Fully left a two-step pattern, then four, eight, "
             "sixteen, and from about a quarter of the way it never repeats.", one(0, Y, [("pot", "r")])),
            ("3 lorenz", "the butterfly · X Y Z",
             "Lorenz: the butterfly. Side by side, Lorenz segments read one attractor: X, Y and Z.",
             dict(cols=cols(Y, Y, Y, None, None, None), badges=[(0, "out", "X"), (1, "out", "Y"), (2, "out", "Z")],
                  dim=[3, 4, 5])),
            ("4–6", "cellular · coin gates · dust",
             "Cellular: an automaton whose rule the knob picks, eight cells per segment, one ring across a row. "
             "Coin gates: a coin toss on every clock. Dust: triggers at random moments, up to fifty a second.",
             one(1, Y, [("pot", "rule"), ("out", "cells")])),
            ("7 stair", "a row is one sequence",
             "Stair: put two to six side by side and they're one stepped sequence. Each slider is the height of "
             "its step, each knob the chance it plays. The first segment's gate is the clock, and it outputs the "
             "stairs; the others give a gate while their step is on.",
             dict(cols=cols(Y, Y, Y, Y, None, None),
                  rings=[(i, "slider") for i in range(4)] + [(i, "pot") for i in range(4)],
                  badges=[(0, "gate", "clock"), (0, "out", "stairs")], dim=[4, 5])),
        ]))

    ch.append(dict(code="env", title="envelope", sub="red · attack and decay in one segment",
        say="Envelope. Attack and decay in one segment: no more patching two together.",
        steps=[
            ("slider · knob", "attack · decay, 1 ms to 10 s",
             "The slider is the attack and the knob the decay, each from one millisecond to ten seconds.",
             one(4, R, [("slider", "attack"), ("pot", "decay")])),
            ("cv", "time, 1 V twice as fast",
             "The C.V. jack is time: plus one volt makes both twice as fast.", one(4, R, [("cv", "time")])),
            ("gate", "trigger or held gate",
             "A trigger plays the attack then the decay. A gate held past the attack stays at the top until you "
             "let go. Retriggering never clicks.", one(4, R, [("gate", "gate"), ("out", "0–8 V")])),
            ("curve · loop", "hold + knob · extra option",
             "Hold and turn the knob for the curve: snappy to the left, slow to start on the right. The extra "
             "option makes it loop.", one(4, R, [("button", "hold"), ("pot", "curve")])),
            ("flavours", "1 to 4 hits",
             "The flavours are ratchets: one, two, three or four hits per trigger.",
             one(4, R, [("led", "1–4")])),
        ]))

    ch.append(dict(code="osc", title="oscillator", sub="green and red · audio, ±5 V",
        say="Oscillator. Audio oscillators, plus or minus five volts, several inspired by the S.D.S. digital V.C.O.",
        steps=[
            ("pitch", "C1 to C7 · 1 V/oct",
             "The slider is the pitch, C one to C seven. The C.V. jack is one volt per octave. Hold and turn "
             "the knob to fine tune.", one(5, "greenred", [("slider", "pitch"), ("cv", "V/oct")])),
            ("knob · gate", "timbre · hard sync",
             "The knob is the timbre, and the gate jack hard sync. The extra option is a voice: a built-in "
             "envelope and V.C.A. the gate opens.", one(5, "greenred", [("pot", "timbre"), ("gate", "sync")])),
            ("1 morph", "sine to pulse",
             "Morph: sine, triangle, saw, square, narrow pulse, smoothly in between, band-limited.",
             one(5, "greenred", [("pot", "shape")])),
            ("2 bank", "16 waveforms",
             "Wave bank: the knob scans sixteen waveforms, crossfading, every one stored band-limited, one table "
             "per octave, so even the hard-edged ones stay clean at high notes.",
             one(5, "greenred", [("pot", "wave")])),
            ("3–5", "additive · pair · fold",
             "Additive: one to sixteen harmonics. Pair: two sines, and between them your ear hears a third, "
             "lower tone. Fold: a sine through a wavefolder, from round to glassy.",
             one(5, "greenred", [("pot", "timbre")])),
            ("6–8", "fm · gendy · rider",
             "F.M.: the segment on the left is the modulator. Gendy: Xenakis' dynamic stochastic synthesis, a "
             "waveform taking a random walk. Rider: it records the segment on its left into its own waveform as "
             "it plays.", dict(cols=cols(None, None, None, None, G, "greenred"),
                               badges=[(4, "out", "mod"), (5, "out", "out")], dim=[0, 1, 2, 3])),
        ]))

    ch.append(dict(code="proc", title="processor", sub="yellow and red · CV in, CV out",
        say="Processor. The C.V. jack becomes an input, read about three thousand nine hundred times a second.",
        steps=[
            ("1–2", "follow · transient",
             "Follow: the BRIBESTAGES envelope follower. Transient: a trigger every time the input jumps, a drum "
             "hit or a pluck.", one(3, "yellowred", [("cv", "in"), ("out", "out")])),
            ("3–4", "slew · crush and fold",
             "Slew, with its own rise and fall. Crush and fold: drive into a folder, and the rate held down.",
             one(3, "yellowred", [("slider", "rise"), ("pot", "fall")])),
            ("5 looper", "gate high records",
             "Looper: a high gate records the C.V. jack, up to twelve seconds, and it loops when the gate goes "
             "low. The slider is the speed.", one(3, "yellowred", [("gate", "record"), ("slider", "speed")])),
            ("6 quantise", "38 scales",
             "Quantise: one volt per octave into Sleepcousonique's thirty-eight scales, from just intonation to "
             "Bohlen-Pierce. The knob is the scale, hold and turn it for the root.",
             one(3, "yellowred", [("pot", "scale"), ("cv", "pitch")])),
            ("7–8", "hold · compare",
             "Hold: sample and hold, or track and hold with the extra option. Compare: a comparator, or a window "
             "around the level.", one(3, "yellowred", [("gate", "sample"), ("slider", "level")])),
        ]))

    ch.append(dict(code="fx", title="effect", sub="green and yellow · the left neighbour, at full rate",
        say="Effect. Audio processing of the segment on the left, sample by sample at the full rate, so an "
            "oscillator, a filter and a V.C.A. across three segments sound clean.",
        steps=[
            ("1–2", "filter · VCA",
             "Filter: cutoff from twenty hertz to thirteen kilohertz, resonance up to ringing, low, band or high "
             "pass. V.C.A.: the gate opens it, and the extra option makes it a low-pass gate.",
             dict(cols=cols(None, None, None, "greenred", "greenyellow", "greenyellow"),
                  badges=[(3, "led", "osc"), (4, "led", "filter"), (5, "led", "vca")], dim=[0, 1, 2])),
            ("3–4", "ring · comb",
             "Ring: a ring modulator with its own carrier. Comb: a pitched comb filter that rings.",
             one(4, "greenyellow", [("slider", "pitch"), ("pot", "ring")])),
            ("5–6", "chorus · echo",
             "Chorus, towards a flanger with feedback. Echo, up to a fifth of a second, warm and dark like a "
             "small tape.", one(4, "greenyellow", [("slider", "time"), ("pot", "fbk")])),
            ("7–8", "drive · lo-fi",
             "Drive: soft clip to wave folder. Lo-fi: the sample rate and the bits, down to a hundred hertz and "
             "two bits.", one(4, "greenyellow", [("slider", "drive"), ("pot", "bits")])),
        ]))

    ch.append(dict(code="cords", title="cords", sub="from the left neighbour, no cable",
        say="Cords. A cord takes the output of the segment on the left into a segment's C.V. input, as if a "
            "patch cable ran there. Segment one's left is segment six: the row is a ring.",
        steps=[
            ("set one", "hold + tap the button on its left",
             "Hold the segment's button and tap the button on its left: each tap steps to the next kind.",
             dict(cols=cols(None, None, G, Y, None, None), rings=[(3, "button"), (2, "button")],
                  badges=[(3, "button", "hold"), (2, "button", "tap")], dim=[0, 1, 4, 5])),
            ("kinds", "hall · fog · water · door · range · maybe",
             "Hall is a plain wire. Fog rounds the edges. Water is slow to fill and slow to drain. Door lets "
             "through only what's above zero. Range turns the swing into zero to the amount. Maybe rolls the "
             "dice on every rise.",
             dict(cols=cols(None, None, G, Y, None, None), rings=[(2, "led")], badges=[(2, "led", "kind")],
                  dim=[0, 1, 4, 5])),
            ("amount", "hold both + turn the knob",
             "Hold both buttons and turn the held segment's knob for the amount, minus one to plus one.",
             dict(cols=cols(None, None, G, Y, None, None), rings=[(3, "pot"), (2, "button"), (3, "button")],
                  badges=[(3, "pot", "amount")], dim=[0, 1, 4, 5])),
        ]))

    ch.append(dict(code="drift", title="dream drift", sub="buttons 1 and 2, 2 seconds",
        say="Dream drift.",
        steps=[
            ("on · off", "hold 1 and 2 for 2 s",
             "Hold buttons one and two together for two seconds: all the lights flash green for on, red for off.",
             dict(cols=cols(G, G, G, G, G, G), rings=[(0, "button"), (1, "button")],
                  badges=[(0, "button", "hold"), (1, "button", "hold")])),
            ("dreaming", "a minute alone, then it wanders",
             "A minute after you last touched anything, every slider and knob starts to wander on a slow Lorenz "
             "attractor, up to a sixth of their travel. Touch anything and it comes back in half a second.",
             dict(cols=DEFAULT, rings=[(i, "slider") for i in range(6)] + [(i, "pot") for i in range(6)])),
        ]))

    ch.append(dict(code="recipes", title="recipes", sub="a few to start from",
        say="A few recipes.",
        steps=[
            ("a voice", "quantised random into an oscillator",
             "A whole voice from two segments. Segment five a quantised random, patched into C.V. six. Segment "
             "six an oscillator with the voice option. One clock into both gates: a new note every clock.",
             dict(cols=cols(None, None, None, None, G, "greenred"), rings=[(4, "out"), (5, "cv"), (4, "gate"), (5, "gate")],
                  badges=[(4, "out", "note"), (5, "cv", "pitch")], dim=[0, 1, 2, 3])),
            ("three segments", "oscillator → filter → VCA",
             "A voice across three segments: four an oscillator, five a filter effect, six a V.C.A. effect, a "
             "clock into gate six. No cables between them.",
             dict(cols=cols(None, None, None, "greenred", "greenyellow", "greenyellow"),
                  rings=[(5, "gate")], badges=[(5, "gate", "clock")], dim=[0, 1, 2])),
            ("the butterfly", "three Lorenz segments",
             "Segments one to three chaos, Lorenz each: three related voltages that never repeat.",
             dict(cols=cols(Y, Y, Y, None, None, None), badges=[(0, "out", "X"), (1, "out", "Y"), (2, "out", "Z")],
                  dim=[3, 4, 5])),
            ("rain", "weather into a drum",
             "Rain on a drum: segments one to three weather, slider up. Out three is the rain: patch it into a "
             "drum. Out two, the wind, into a filter's cutoff.",
             dict(cols=cols(G, G, G, None, None, None),
                  badges=[(1, "out", "wind"), (2, "out", "rain")], dim=[3, 4, 5])),
        ]))

    ch.append(dict(code="outro", title="Cirrostratus Stages", sub="part of Cirrostratus",
        say="That's Cirrostratus Stages. Thanks to Mutable Instruments and Émilie Gillet for Stages, to qiemem, and to "
            "BRIBESTAGES. The manual, the firmware and the source are on the Cirrostratus page.",
        steps=[
            ("thanks", "Mutable Instruments · qiemem · BRIBESTAGES",
             "Every segment, its own instrument.", dict(cols=DEFAULT)),
        ]))
    return ch


# ---------------------------------------------------------------------------
# Frames.

def readable(text):
    """The caption on screen: initials as written (C.V. is said, CV is read)."""
    return re.sub(r"\b((?:[A-Z]\.){2,})", lambda m: m.group(1).replace(".", ""), text)


def wrap(text, fnt, width, d):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if d.textlength(t, font=fnt) > width and cur:
            lines.append(cur)
            cur = w
        else:
            cur = t
    if cur:
        lines.append(cur)
    return lines


def panel_image(fig, work, key):
    path = os.path.join(work, "panel_%s.png" % key)
    if not os.path.exists(path):
        panel.draw(path, cols=fig.get("cols"), rings=fig.get("rings", ()), badges=fig.get("badges", ()),
                   dim=fig.get("dim", ()), bg=GROUND)
    img = Image.open(path).convert("RGB")
    h = 1000
    return img.resize((int(img.width * h / img.height), h), Image.LANCZOS)


def frame(c, n_ch, active, work):
    img = Image.new("RGB", (W, H), GROUND)
    d = ImageDraw.Draw(img)
    # a soft band behind the text column
    d.rectangle([700, 0, W, H], fill=GROUND2)
    step = c["steps"][active] if active is not None else c["steps"][0]
    fig = step[3] if active is not None else c.get("fig", step[3])
    key = hashlib.sha1(json.dumps(fig, sort_keys=True, default=str).encode()).hexdigest()[:12]
    p = panel_image(fig, work, key)
    img.paste(p, (40 + (640 - p.width) // 2, 40))
    x = 770
    d.text((x, 70), c["title"], font=F_TITLE, fill=INK)
    d.text((x, 165), c["sub"], font=F_SUB, fill=ACCENT)
    d.text((W - 60 - d.textlength("%d / %d" % (c["index"] + 1, n_ch), font=F_SMALL), 40),
           "%d / %d" % (c["index"] + 1, n_ch), font=F_SMALL, fill=DIM)
    y = 260
    steps = c["steps"]
    first = 0
    if len(steps) > 6:   # scroll so the active step stays in view
        first = max(0, min((active or 0) - 2, len(steps) - 6))
    for i, s in list(enumerate(steps))[first:first + 6]:
        on = i == active
        if on:
            d.rectangle([x - 22, y - 6, x - 14, y + 62], fill=ACCENT)
        d.text((x, y), s[0], font=F_LABEL, fill=INK if on else DIM)
        d.text((x, y + 32), s[1], font=F_ROW, fill=INK if on else DIM)
        y += 82
        if y > 760:
            break
    # the caption: what she's saying, on screen
    text = readable(c["say"] if active is None else step[2])
    lines = wrap(text, F_CAP, W - x - 80, d)[:6]
    cy = H - 60 - len(lines) * 40
    d.rectangle([x - 22, cy - 22, W - 40, H - 40], fill=STRIP)
    for i, ln in enumerate(lines):
        d.text((x, cy + i * 40), ln, font=F_CAP, fill=INK)
    return img


# ---------------------------------------------------------------------------

def encode_chapter(args):
    stills, out = args   # [(png, seconds)]
    inputs, filt = [], []
    n = len(stills)
    for i, (png, dur) in enumerate(stills):
        t = dur + (FADE if i < n - 1 else 0.0)
        inputs += ["-loop", "1", "-framerate", str(FPS), "-t", "%.3f" % t, "-i", png]
    if n == 1:
        filt = "[0:v]format=yuv420p[v]"
    else:
        chain, acc, last = [], 0.0, "[0:v]"
        for i in range(1, n):
            acc += stills[i - 1][1]
            lab = "[x%d]" % i if i < n - 1 else "[v0]"
            chain.append("%s[%d:v]xfade=transition=fade:duration=%.2f:offset=%.3f%s" % (last, i, FADE, acc, lab))
            last = lab
        filt = ";".join(chain) + ";[v0]format=yuv420p[v]"
    subprocess.run(["ffmpeg", "-v", "error", "-y"] + inputs +
                   ["-filter_complex", filt, "-map", "[v]", "-r", str(FPS), "-c:v", "libx264", "-preset", "medium",
                    "-crf", "20", "-tune", "stillimage", out], check=True)
    return out


def load_music(path, seconds):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ar", str(SR), "-ac", "2", "-f", "f32le", "-"],
                         check=True, stdout=subprocess.PIPE).stdout
    m = np.frombuffer(raw, np.float32).reshape(-1, 2).copy()
    need = int(seconds * SR) + SR
    xf = 4 * SR
    while len(m) < need:   # loop with a 4 s crossfade
        ramp = np.linspace(0, 1, xf)[:, None]
        head = m[:xf] * ramp
        m = np.concatenate([m[:-xf], m[-xf:] * (1 - ramp) + head, m[xf:]])
    return m[:need]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--music", required=True)
    ap.add_argument("--out", default=os.path.expanduser("~/Desktop/Cirrostratus-Stages-Video-Manual-1.3.1.mp4"))
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "video"))
    ap.add_argument("--only", help="chapter range a-b (for previews)")
    ap.add_argument("--still", action="store_true", help="one frame per chapter as PNG, then stop")
    ap.add_argument("--print", action="store_true", help="print the spoken text and stop")
    ap.add_argument("--music-level", type=float, default=0.75)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 2))
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)

    chs = chapters()
    for i, c in enumerate(chs):
        c["index"] = i
    if a.only:
        lo, hi = [int(v) for v in a.only.split("-")]
        chs = chs[lo:hi + 1]
    n_ch = len(chapters())

    if a.print:
        for c in chs:
            print("[%s] %s" % (c["code"], speakable(c["say"])))
            for s in c["steps"]:
                print("[%s/%s] %s" % (c["code"], s[0], speakable(s[2])))
        return
    if a.still:
        for c in chs:
            frame(c, n_ch, 0, a.work).save(os.path.join(a.work, "still_%s.png" % c["code"]))
        print(a.work)
        return

    # speech: per chapter, the title line, then every step
    texts = []
    for c in chs:
        texts.append(c["say"])
        texts += [s[2] for s in c["steps"]]
    voices = speak_all(texts, a.work)

    gap, chapter_gap = 0.55, 0.9
    k = 0
    narration, plan, marks, t = [], [], [], 0.0
    for c in chs:
        marks.append((t, c["title"]))
        stills = []
        for j in range(-1, len(c["steps"])):
            v = voices[k]
            k += 1
            dur = len(v) / SR + (gap if j < len(c["steps"]) - 1 else chapter_gap)
            if j == -1:
                dur += 0.4   # a breath before she starts
                narration.append(np.zeros(int(0.4 * SR), np.float32))
            narration.append(v)
            narration.append(np.zeros(int(dur * SR) - len(v) - (int(0.4 * SR) if j == -1 else 0), np.float32))
            png = os.path.join(a.work, "f_%s_%d.png" % (c["code"], j + 1))
            frame(c, n_ch, None if j == -1 else j, a.work).save(png)
            stills.append((png, dur))
            t += dur
        plan.append((stills, os.path.join(a.work, "ch_%02d_%s.mp4" % (c["index"], c["code"]))))
        print("frames %s" % c["code"], flush=True)

    with ThreadPoolExecutor(a.jobs) as ex:
        parts = list(ex.map(encode_chapter, plan))
    lst = os.path.join(a.work, "parts.txt")
    with open(lst, "w") as f:
        for p in parts:
            f.write("file '%s'\n" % p)

    voice = np.concatenate(narration)
    total = len(voice) / SR
    music = load_music(a.music, total)[:len(voice)]
    # duck: the music sits low while she speaks, rises a little in the gaps
    env = np.abs(voice)
    win = int(0.25 * SR)
    speaking = np.convolve(env > 0.01, np.ones(win) / win, mode="same") > 0.02
    target = np.where(speaking, 0.10, 0.22) * a.music_level
    g = np.empty_like(target)
    acc = target[0]
    a_up, a_dn = 1.0 / (0.6 * SR), 1.0 / (0.12 * SR)
    for i in range(0, len(target), 64):   # per 64 samples is plenty
        tg = target[i]
        acc += (tg - acc) * (a_dn if tg < acc else a_up) * 64
        g[i:i + 64] = acc
    fade = np.ones(len(voice))
    fs = int(3 * SR)
    fade[:fs] = np.linspace(0, 1, fs)
    fade[-fs:] = np.linspace(1, 0, fs)
    mix = music * (g * fade)[:, None] + voice[:, None] * 0.9
    mix = np.clip(mix, -1.0, 1.0).astype(np.float32)
    audio = os.path.join(a.work, "audio.f32")
    mix.tofile(audio)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", lst,
                    "-f", "f32le", "-ar", str(SR), "-ac", "2", "-i", audio,
                    "-map", "0:v", "-map", "1:a", "-c:v", "copy", "-c:a", "aac", "-b:a", "192k",
                    "-shortest", "-movflags", "+faststart", a.out], check=True)
    chap = os.path.splitext(a.out)[0] + "-YouTube-Chapters.txt"
    with open(chap, "w") as f:
        for tm, title in marks:
            f.write("%d:%02d %s\n" % (int(tm) // 60, int(tm) % 60, title))
    print("total %.1f s" % total)
    print(a.out)


if __name__ == "__main__":
    main()
