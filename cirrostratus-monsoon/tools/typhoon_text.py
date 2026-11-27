"""The mode chapters (docs/modes_*.md) in the Typhoon's words, for the
Typhoon manual (make_manual.py --module typhoon) and video
(build_video.py --module typhoon).

The 30 modes are the same on both modules; only the names on the panel
differ (docs/TYPHOON.md): DISP is TIME, WRITE is BANK, the three per-mode
knobs BAL / FBK / RVB are STEREO / FEEDBACK / REVERB, and GAIN is IN LEVEL.
The Monsoon's bank colours (green = bank 1, red = bank 2) are the BANK
column's A and B LEDs on the Typhoon.

The mode chapters were checked by hand for anything else that is
Monsoon-only (LED colours, jack names): the bank headings are the only
place. check() fails loudly if a later edit adds one."""
import re

# Whole words only: "BAL" must not touch "BALANCE", "RVB" not "RVBX".
NAMES = [("BAL", "STEREO"), ("FBK", "FEEDBACK"), ("RVB", "REVERB"),
         ("DISP", "TIME"), ("WRITE", "BANK"), ("GAIN", "IN LEVEL")]

BANKS = [(r"^### Bank 1 \(green\)(.*)$", r"### Bank 1 (BANK LED A)\1"),
         (r"^### Bank 2 \(red\)\s*$", "### Bank 2 (BANK LED B) · 16–30")]

# Words that would mean a Monsoon LED colour or control if they turned up.
SUSPECT = re.compile(r"\b(green|red|amber|bicolou?r|pencil|DISP|WRITE|GAIN|BAL|FBK|RVB)\b")


def typhoonize(text):
    for pat, rep in BANKS:
        text = re.sub(pat, rep, text, flags=re.M)
    for a, b in NAMES:
        text = re.sub(r"\b%s\b" % a, b, text)
    check(text)
    return text


def check(text):
    bad = [m.group(0) for m in SUSPECT.finditer(text)]
    if bad:
        raise SystemExit("typhoon_text: Monsoon-only words left in the mode chapters: %s"
                         % ", ".join(sorted(set(bad))))
