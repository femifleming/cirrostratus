#!/usr/bin/env python3
"""Builds docs/Dobbeltrap-Manual.pdf (and docs/img/*.png) from docs/MANUAL.md.

A <!-- FIG name --> marker becomes a drawing of the Stages panel (panel.py)
in the state FIGS[name] describes. Needs Pillow, `markdown` and Chrome."""
import os, re, subprocess, sys, tempfile
import markdown

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import panel

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, "docs")
IMG = os.path.join(DOCS, "img")
FONTS = os.path.expanduser("~/faltung-kit/assets/fonts")

G, Y, R, GR, YR = "green", "yellow", "red", "greenred", "yellowred"


def default_cols():
    return [dict(led=G, glow=0.7, slider=0.55, pot=0.3), dict(led=G, glow=0.3, slider=0.4, pot=0.6),
            dict(led=G, glow=0.9, slider=0.7, pot=0.1), dict(led=G, glow=0.5, slider=0.3, pot=0.8),
            dict(led=R, glow=0.8, slider=0.2, pot=0.5), dict(led=GR, glow=1.0, slider=0.5, pot=0.4)]


def cols_with(changes):
    c = default_cols()
    for i, d in changes.items():
        c[i].update(d)
    return c


FIGS = {
    "overview": dict(cols=default_cols(),
                     badges=[(0, "led", "Random"), (4, "led", "Env"), (5, "led", "Osc")]),
    "install": dict(cols=[dict(led=None, glow=0) for _ in range(5)] + [dict(led=None, glow=1.0, slider=0.5)],
                    rings=[(5, "cv"), (5, "button"), (5, "slider")],
                    badges=[(5, "cv", "audio in"), (5, "button", "hold"), (5, "slider", "level")],
                    dim=[0, 1, 2, 3, 4]),
    "enter": dict(cols=[dict(led=None) for _ in range(5)] + [dict(led=R)],
                  rings=[(5, "button")], badges=[(5, "button", "hold 5 s")], dim=[0, 1, 2, 3, 4]),
    "controls": dict(cols=cols_with({}),
                     rings=[(0, "pot"), (0, "led"), (0, "button"), (0, "slider"), (0, "cv"), (0, "gate"), (0, "out")],
                     badges=[(0, "pot", "knob"), (0, "led", "light"), (0, "button", "button"),
                             (0, "slider", "slider"), (0, "cv", "CV"), (0, "gate", "gate"), (0, "out", "out")],
                     dim=[1, 2, 3, 4, 5]),
    "flavours": dict(cols=[dict(led=G), dict(led=Y), dict(led=R), dict(led=G, blink=True),
                           dict(led=Y, blink=True), dict(led=R, blink=True)],
                     badges=[(i, "led", str(i + 1)) for i in range(6)]),
    "random": dict(cols=[dict(led=G, glow=0.8, slider=0.55, pot=0.3), dict(led=G, glow=0.3, slider=0.4),
                         dict(led=G, glow=0.6), dict(led=G, glow=0.2), dict(led=R), dict(led=GR)],
                   rings=[(0, "slider"), (0, "pot"), (0, "gate"), (0, "cv"), (0, "out")],
                   badges=[(0, "slider", "rate"), (0, "pot", "slew"), (0, "gate", "clock"),
                           (0, "cv", "rate"), (0, "out", "out")],
                   dim=[4, 5]),
    "chaos": dict(cols=[dict(led=Y, glow=0.7, slider=0.6, pot=0.4), dict(led=Y, glow=0.4, slider=0.6),
                        dict(led=Y, glow=0.9, slider=0.6), dict(led=G), dict(led=R), dict(led=GR)],
                  rings=[(0, "slider"), (0, "pot"), (0, "out"), (1, "out"), (2, "out")],
                  badges=[(0, "slider", "speed"), (0, "pot", "ρ"), (0, "out", "X"), (1, "out", "Y"),
                          (2, "out", "Z")],
                  dim=[3, 4, 5]),
    "envelope": dict(cols=cols_with({4: dict(glow=1.0, slider=0.25, pot=0.45)}),
                     rings=[(4, "slider"), (4, "pot"), (4, "gate"), (4, "cv"), (4, "out")],
                     badges=[(4, "slider", "attack"), (4, "pot", "decay"), (4, "gate", "gate"),
                             (4, "cv", "time"), (4, "out", "out")],
                     dim=[0, 1, 2, 3, 5]),
    "oscillator": dict(cols=cols_with({5: dict(glow=1.0, slider=0.6, pot=0.35)}),
                       rings=[(5, "slider"), (5, "pot"), (5, "gate"), (5, "cv"), (5, "out")],
                       badges=[(5, "slider", "pitch"), (5, "pot", "timbre"), (5, "gate", "sync/VCA"),
                               (5, "cv", "V/oct"), (5, "out", "audio")],
                       dim=[0, 1, 2, 3, 4]),
    "processor": dict(cols=cols_with({3: dict(led=YR, glow=0.6, slider=0.5, pot=0.4)}),
                      rings=[(3, "cv"), (3, "slider"), (3, "pot"), (3, "gate"), (3, "out")],
                      badges=[(3, "cv", "in"), (3, "slider", "1st"), (3, "pot", "2nd"),
                              (3, "gate", "gate"), (3, "out", "out")],
                      dim=[0, 1, 2, 4, 5]),
    "neighbours": dict(cols=[dict(led=G, glow=0.6), dict(led=G, glow=0.6), dict(led=G, glow=0.6),
                             dict(led=G), dict(led=GR, glow=1.0), dict(led=GR, glow=1.0)],
                       rings=[(0, "out"), (1, "out"), (2, "out"), (4, "out"), (5, "out")],
                       badges=[(0, "out", "X"), (1, "out", "Y"), (2, "out", "r"), (4, "out", "mod"),
                               (5, "out", "FM")],
                       dim=[3]),
}

CSS = """
 @font-face { font-family: Inter; src: url("file://%(f)s/Inter-Regular.ttf"); font-weight: 400; }
 @font-face { font-family: Inter; src: url("file://%(f)s/Inter-Medium.ttf"); font-weight: 600; }
 @font-face { font-family: Plex; src: url("file://%(f)s/IBMPlexMono-Regular.ttf"); }
 @page { size: A4; margin: 15mm 15mm; }
 body { font: 9.6pt/1.5 Inter, sans-serif; color: #24262b; }
 h1 { font-weight: 600; font-size: 40pt; margin: 4mm 0 0; letter-spacing: -1pt; }
 h1 + h3 { color: #00849a; font-size: 14pt; font-weight: 400; margin-top: 0; }
 h2 { font-weight: 600; font-size: 17pt; color: #00849a; border-bottom: 1.5pt solid #00849a;
      padding-bottom: 3pt; margin: 22pt 0 8pt; clear: both; break-after: avoid; }
 h2.page { break-before: page; margin-top: 0; }
 h3 { font-weight: 600; font-size: 12pt; margin: 14pt 0 4pt; break-after: avoid; }
 section { display: flow-root; break-inside: avoid; }
 img.panel { float: right; width: 62mm; margin: 0 0 8pt 7mm; }
 table { border-collapse: collapse; margin: 4pt 0 10pt; break-inside: avoid; font-size: 9pt; }
 th, td { border-bottom: 0.5pt solid #cfd3d8; padding: 3pt 5pt; text-align: left; vertical-align: top; }
 th { color: #00849a; border-bottom: 1pt solid #00849a; }
 td:first-child { font-weight: 600; }
 code { font: 8.5pt Plex, Menlo, monospace; }
 hr { display: none; }
 a { color: #00849a; }
"""


def build():
    os.makedirs(IMG, exist_ok=True)
    text = open(os.path.join(DOCS, "MANUAL.md")).read()

    def fig(m):
        name = m.group(1)
        panel.draw(os.path.join(IMG, name + ".png"), **FIGS[name])
        return '<img class="panel" src="img/%s.png" width="260" align="right" alt="Stages panel: %s">\n' % (name, name)

    text = re.sub(r"<!-- FIG (\w+) -->\n", fig, text)
    open(os.path.join(DOCS, "Dobbeltrap-Manual.md"), "w").write(text)
    body = markdown.markdown(text, extensions=["tables"])
    body = re.sub(r"<h2>(?=(1|2|3|4|5|6|7|8|9|10|11)\. )", '<h2 class="page">', body)
    parts = re.split(r"(?=<h[23][ >])", body)
    body = parts[0] + "".join("<section>%s</section>" % p for p in parts[1:])
    html = ('<!doctype html><html><head><meta charset="utf-8"><title>Dobbeltrap</title><style>'
            + CSS % dict(f=FONTS) + "</style></head><body>" + body + "</body></html>")
    html_path = os.path.join(DOCS, "manual.html")
    open(html_path, "w").write(html)
    pdf = os.path.join(DOCS, "Dobbeltrap-Manual.pdf")
    if os.path.exists(pdf):
        os.remove(pdf)
    chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    subprocess.run([chrome, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    "--allow-file-access-from-files",
                    "--user-data-dir=" + tempfile.mkdtemp(prefix="dobbel_chrome_"),
                    "--print-to-pdf=" + pdf, "file://" + html_path],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(pdf) or os.path.getsize(pdf) < 10000:
        raise SystemExit("PDF was not written")
    os.remove(html_path)
    print(pdf)


if __name__ == "__main__":
    build()
