#!/usr/bin/env python3
"""Builds docs/Cirrostratus-Manual.md and .pdf from docs/MANUAL.md and the
per-bank mode chapters (docs/modes_*.md), with Monsoon panel figures drawn
like the video manual (tools/manual_figs.py) in docs/img/.

Figures: a <!-- PANEL name --> marker in MANUAL.md becomes the figure of
that name (manual_figs.GENERAL). Every mode's control table (and the panel
table in section 2) gets its own figure, and each row that touches the panel
gets a circled number that matches a badge on the drawing.

Needs `markdown`, Pillow, numpy, the faltung-kit fonts and Chrome."""
import glob, os, re, subprocess, tempfile, markdown

import manual_figs as figs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
IMG = os.path.join(DOCS, "img")
FONTS = os.path.expanduser("~/faltung-kit/assets/fonts")


def img_tag(name, alt):
    return '<img class="panel" src="img/%s.png" width="300" align="right" alt="%s">' % (name, alt)


def figure_table(lines, name, alt, leds, gi):
    """Number a control table, render its figure, return the new lines."""
    rows = [[c.strip() for c in ln.strip().strip("|").split("|")] for ln in lines[2:]]
    labels, badges = figs.number_table([(r[0], r[1] if len(r) > 1 else "") for r in rows])
    figs.render(os.path.join(IMG, name + ".png"), badges=badges, leds=leds, gi=gi)
    out = [img_tag(name, alt), ""] + lines[:2]
    for ln, mark in zip(lines[2:], labels):
        if mark:
            ln = re.sub(r"^\|\s*", "| %s " % mark, ln, count=1)
        out.append(ln)
    return out


def add_figures(text):
    lines = text.split("\n")
    out, i, mode = [], 0, None
    while i < len(lines):
        ln = lines[i]
        m = re.match(r"#### (\d+) · (\S+)", ln)
        if m:
            mode = (int(m.group(1)), m.group(2))
        elif re.match(r"#{1,3} ", ln):
            mode = None
        pm = re.match(r"\s*<!-- PANEL (\w+) -->", ln)
        if pm:
            name = pm.group(1)
            figs.render(os.path.join(IMG, name + ".png"), **figs.GENERAL[name])
            out += [img_tag(name, "Monsoon panel: " + name), ""]
            i += 1
            continue
        if ln.startswith("| Control") or ln.startswith("| On the panel"):
            j = i
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            if mode:
                num, nm = mode
                out += figure_table(lines[i:j], "mode%02d" % num, "Monsoon panel: mode %d %s" % (num, nm),
                                    figs.mode_leds(num), num)
            else:
                out += figure_table(lines[i:j], "panel", "Monsoon panel", [figs.GREEN] * 4, 0)
            i = j
            continue
        out.append(ln)
        i += 1
    return "\n".join(out)


CSS = """
 @font-face { font-family: Inter; src: url("file://%(f)s/Inter-Regular.ttf"); font-weight: 400; }
 @font-face { font-family: Inter; src: url("file://%(f)s/Inter-Medium.ttf"); font-weight: 600; }
 @font-face { font-family: Plex; src: url("file://%(f)s/IBMPlexMono-Regular.ttf"); font-weight: 400; }
 @font-face { font-family: Plex; src: url("file://%(f)s/IBMPlexMono-Medium.ttf"); font-weight: 600; }
 @page { size: A4; margin: 0; }
 html { background: #0f1a2e; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
 body { font: 9.6pt/1.5 Inter, -apple-system, sans-serif; color: #f6f6f0; margin: 0;
        padding: 15mm; box-decoration-break: clone; -webkit-box-decoration-break: clone; }
 h1 { font-weight: 600; font-size: 40pt; margin: 6mm 0 2pt; letter-spacing: -1pt; color: #f6f6f0; }
 h1 + h3 { color: #95c6db; font-size: 14pt; font-weight: 400; margin-top: 0; }
 h2 { font-weight: 600; font-size: 17pt; margin: 22pt 0 8pt; padding-bottom: 3pt; color: #95c6db;
      border-bottom: 1.5pt solid #5b7aa3; clear: both; break-after: avoid; }
 h2.page { break-before: page; margin-top: 0; }
 h3 { font-weight: 600; font-size: 12pt; margin: 16pt 0 4pt; color: #9fd3ec; break-after: avoid; clear: both; }
 section.mode img.panel { width: 66mm; }
 h3.page { break-before: page; margin-top: 0; }
 h4.nb { break-before: auto; }
 h4 { font-weight: 600; font-size: 20pt; margin: 0 0 2pt; color: #f6f6f0; break-before: page; clear: both;
      letter-spacing: -0.3pt; }
 h4 + p em { color: #95c6db; font-size: 11pt; }
 strong { color: #ffffff; font-weight: 600; }
 em { color: #cfe6f0; }
 a { color: #9fd3ec; }
 section { display: flow-root; break-inside: avoid; }
 img.panel { float: right; width: 58mm; margin: 2pt 0 8pt 7mm; border-radius: 6pt; }
 table { border-collapse: collapse; margin: 4pt 0 10pt; break-inside: avoid; font-size: 9pt; }
 th, td { border-bottom: 0.5pt solid #2c4a7a; padding: 3pt 5pt; text-align: left; vertical-align: top; }
 th { color: #95c6db; font-weight: 600; border-bottom: 1pt solid #5b7aa3; }
 td:first-child { white-space: nowrap; font-weight: 600; color: #cfe6f0; }
 code, pre { font: 8.4pt Plex, Menlo, monospace; color: #cfe6f0; }
 pre { background: #13203a; padding: 7pt 9pt; break-inside: avoid; border-left: 2pt solid #4a9ecf;
       overflow: hidden; clear: both; }
 blockquote { margin: 10pt 0; padding: 4pt 12pt; border-left: 2pt solid #95c6db; color: #cfe6f0; }
 hr { display: none; }
 ul, ol { padding-left: 16pt; }
"""


def build():
    os.makedirs(IMG, exist_ok=True)
    for old in glob.glob(os.path.join(IMG, "*.png")):
        os.remove(old)
    text = open(os.path.join(DOCS, "MANUAL.md")).read()
    # modes_1_14 before modes_15_30: sort by the first number, not as text.
    files = sorted(glob.glob(os.path.join(DOCS, "modes_*.md")),
                   key=lambda f: int(re.search(r"modes_(\d+)", f).group(1)))
    modes = "".join(open(f).read() + "\n" for f in files)
    text = add_figures(text.replace("<!-- MODES -->", modes))
    md_out = os.path.join(DOCS, "Cirrostratus-Manual.md")
    open(md_out, "w").write(text)
    body = markdown.markdown(text, extensions=["tables", "fenced_code"])
    body = re.sub(r"<h2>(?=(1|2|3|5)\. )", '<h2 class="page">', body)
    # A bank heading opens a page and takes its first mode along.
    body = re.sub(r"<h3>(Bank 2[^<]*</h3>\s*)<h4>", r'<h3 class="page">\1<h4 class="nb">', body)
    body = re.sub(r"(<h3>Bank 1[^<]*</h3>\s*)<h4>", r'\1<h4 class="nb">', body)
    # One <section> per heading, so a figure stays on the page with its text.
    parts = re.split(r"(?=<h[234][ >])", body)
    body = parts[0] + "".join('<section class="%s">%s</section>' % ("mode" if p.startswith("<h4") else "", p)
                              for p in parts[1:])
    html = ('<!doctype html><html><head><meta charset="utf-8"><title>Cirrostratus</title><style>'
            + CSS % dict(f=FONTS) + "</style></head><body>" + body + "</body></html>")
    html_path = os.path.join(DOCS, "manual.html")
    open(html_path, "w").write(html)
    pdf = os.path.join(DOCS, "Cirrostratus-Manual.pdf")
    if os.path.exists(pdf):
        os.remove(pdf)
    chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    subprocess.run([chrome, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    "--allow-file-access-from-files",
                    "--user-data-dir=" + tempfile.mkdtemp(prefix="cirro_chrome_"),
                    "--print-to-pdf=" + pdf, "file://" + html_path],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # Headless Chrome sometimes crashes on shutdown after writing the file,
    # so judge success by the PDF, not the exit code.
    if not os.path.exists(pdf) or os.path.getsize(pdf) < 10000:
        raise SystemExit("PDF was not written")
    os.remove(html_path)
    print(md_out)
    print(pdf)


if __name__ == "__main__":
    build()
