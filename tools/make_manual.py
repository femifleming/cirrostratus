#!/usr/bin/env python3
"""Builds docs/Monsoon-HiFi-Manual.md and .pdf from docs/MANUAL.md and the
per-bank mode chapters (docs/modes_*.md). Needs `markdown` and Chrome."""
import glob, os, subprocess, markdown

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")

def build():
    text = open(os.path.join(DOCS, "MANUAL.md")).read()
    modes = "".join(open(f).read() + "\n" for f in sorted(glob.glob(os.path.join(DOCS, "modes_*.md"))))
    text = text.replace("<!-- MODES -->", modes)
    md_out = os.path.join(DOCS, "Monsoon-HiFi-Manual.md")
    open(md_out, "w").write(text)
    body = markdown.markdown(text, extensions=["tables", "fenced_code"])
    html = """<!doctype html><html><head><meta charset="utf-8"><title>Monsoon HiFi</title>
<style>
 @page { size: A4; margin: 16mm 15mm; }
 body { font: 10pt/1.45 -apple-system, "Helvetica Neue", Helvetica, Arial, sans-serif; color: #1c1c1c; }
 h1 { font-size: 26pt; margin: 0 0 4pt; letter-spacing: -0.5pt; }
 h2 { font-size: 15pt; margin: 22pt 0 6pt; border-bottom: 1.5pt solid #1c1c1c; padding-bottom: 2pt; page-break-after: avoid; }
 h3 { font-size: 12pt; margin: 16pt 0 4pt; page-break-after: avoid; }
 h4 { font-size: 11pt; margin: 14pt 0 3pt; page-break-after: avoid; }
 table { border-collapse: collapse; width: 100%; margin: 4pt 0 8pt; page-break-inside: avoid; }
 th, td { border-bottom: 0.5pt solid #bbb; padding: 2.5pt 5pt; text-align: left; vertical-align: top; }
 th { background: #eee; }
 td:first-child { white-space: nowrap; font-weight: 600; }
 code, pre { font: 8.5pt "SF Mono", Menlo, monospace; }
 pre { background: #f3f3f3; padding: 6pt 8pt; page-break-inside: avoid; }
 hr { border: 0; border-top: 0.5pt solid #ccc; margin: 14pt 0; }
</style></head><body>""" + body + "</body></html>"
    html_path = os.path.join(DOCS, "manual.html")
    open(html_path, "w").write(html)
    pdf = os.path.join(DOCS, "Monsoon-HiFi-Manual.pdf")
    chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    subprocess.run([chrome, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    "--print-to-pdf=" + pdf, "file://" + html_path],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.remove(html_path)
    print(md_out)
    print(pdf)

if __name__ == "__main__":
    build()
