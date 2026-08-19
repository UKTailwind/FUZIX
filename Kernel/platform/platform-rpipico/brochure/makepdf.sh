#!/bin/sh
#
# Rebuild fuzix-pc3-brochure.pdf from the three artboard sources.
#
# The .dc.html files are Claude Design components: ordinary HTML inside
# an <x-dc> wrapper, with the theme colour as a {{accent}} hole and the
# board photo referenced by filename.  This strips the wrapper, pins the
# accent, inlines the photo, stacks the three pages with A4 page breaks
# and prints the result.  Nothing here is needed to EDIT the brochure -
# that happens on the canvas - only to produce the PDF from what the
# canvas saved.
#
# Needs python3 and a Chromium-family browser.  Under WSL, where the
# only browser is usually a Windows one, the page is staged into
# C:\Windows\Temp first: a Windows .exe cannot read or write a /home
# path, and headless Chrome fails the WRITE loudly and the READ
# silently - a blank PDF with no error at all.

set -e
cd "$(dirname "$0")"

python3 - <<'PY'
import re, base64
img = base64.b64encode(open("board.jpg", "rb").read()).decode()
bodies = []
for name in ("Main", "Inside", "Specs"):
    s = open(name + ".dc.html", encoding="utf-8").read()
    s = re.sub(r'<script data-dc-script.*?</script>', '', s, flags=re.S)
    s = s.replace('{{accent}}', '#c62d1f')
    s = s.replace('src="board.jpg"', 'src="data:image/jpeg;base64,%s"' % img)
    body = s.split('<x-dc>', 1)[1].split('</x-dc>', 1)[0]
    bodies.append(re.sub(r'<helmet>.*?</helmet>', '', body, flags=re.S).strip())

head = '''<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wght@400;500;600;700;800&family=IBM+Plex+Mono:wght@400;500;600&display=swap">
<style>
  @page { size: 210mm 297mm; margin: 0; }
  html, body { margin: 0; padding: 0; background: #fff; }
  * { box-sizing: border-box; }
  a { color: #c62d1f; text-decoration: none; }
  .page { page-break-after: always; break-after: page; }
  .page:last-child { page-break-after: auto; break-after: auto; }
</style></head><body>
'''
open("print.html", "w", encoding="utf-8").write(
    head + "\n".join('<div class="page">%s</div>' % b for b in bodies) + "\n</body></html>\n")
print("wrote print.html")
PY

OUT="$PWD/fuzix-pc3-brochure.pdf"
FLAGS="--headless=new --disable-gpu --no-pdf-header-footer --virtual-time-budget=8000"

# A native browser can work in place.
BROWSER=
for b in chromium chromium-browser google-chrome google-chrome-stable; do
	command -v "$b" >/dev/null 2>&1 && { BROWSER=$b; break; }
done

if [ -n "$BROWSER" ]; then
	"$BROWSER" $FLAGS --print-to-pdf="$OUT" "file://$PWD/print.html"
else
	# WSL: a Windows browser, through a directory Windows can see.
	WIN=
	for b in "/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
		 "/mnt/c/Program Files/Microsoft/Edge/Application/msedge.exe" \
		 "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe"; do
		[ -x "$b" ] && { WIN=$b; break; }
	done
	if [ -z "$WIN" ] || ! command -v wslpath >/dev/null 2>&1; then
		echo "makepdf.sh: no chromium/chrome/edge found - print.html is ready," >&2
		echo "            open it in a browser and Print to PDF: A4, no" >&2
		echo "            margins, background graphics on." >&2
		exit 1
	fi
	STAGE=$(mktemp -d /mnt/c/Windows/Temp/pc3brochureXXXXXX)
	cp print.html "$STAGE/print.html"
	"$WIN" $FLAGS --print-to-pdf="$(wslpath -w "$STAGE/out.pdf")" \
		"file:///$(wslpath -w "$STAGE/print.html")"
	cp "$STAGE/out.pdf" "$OUT"
	rm -rf "$STAGE"
fi

# Headless Chrome reports success on an empty render, so check the shape
# of what came out rather than trusting the exit status.
python3 - "$OUT" <<'PY'
import re, sys
d = open(sys.argv[1], "rb").read()
n = int(re.search(rb"/Count\s+(\d+)", d).group(1))
box = re.search(rb"/MediaBox\s*\[\s*0\s+0\s+(\d+)", d).group(1).decode()
if n != 3 or box != "594":
    sys.exit("makepdf.sh: expected 3 A4 pages, got %d pages at width %s pt" % (n, box))
print("wrote fuzix-pc3-brochure.pdf - %d pages, A4, %d KB" % (n, len(d) // 1024))
PY

rm -f print.html
