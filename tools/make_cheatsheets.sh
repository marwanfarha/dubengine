#!/bin/sh
# The cheat sheets (docs/cheatsheets/*.html) printed to A4 PDFs with headless Chrome, one page each; with --png, each
# also rendered to build/cheatsheets/<name>.png (110 dpi) to look at.
#   tools/make_cheatsheets.sh [--png] [name ...]      (no names: every sheet)
# A sheet with a template (build/cheatsheets/src/<name>.tpl.html) is spliced first (its drawings and screens inlined).
set -eu
cd "$(dirname "$0")/.."
CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
DIR=docs/cheatsheets
png=0
if [ "${1:-}" = "--png" ]; then png=1; shift; fi
if [ $# -eq 0 ]; then
    set -- $(ls "$DIR"/*.html | sed 's|.*/||; s|\.html$||')
fi
fail=0
for name in "$@"; do
    html="$PWD/$DIR/$name.html"
    pdf="$DIR/$name.pdf"
    if [ -f "build/cheatsheets/src/$name.tpl.html" ]; then
        python3 -I build/cheatsheets/src/splice.py "$name"
    fi
    [ -f "$html" ] || { echo "make_cheatsheets: no $html"; exit 1; }
    # (the fonts come from Google Fonts: the virtual time lets them load before the print)
    "$CHROME" --headless --disable-gpu --no-pdf-header-footer --virtual-time-budget=10000 \
        --print-to-pdf="$pdf" "file://$html" 2>/dev/null || true   # (its teardown can end non-zero after the PDF is written: the checks below judge)
    pages=$(pdfinfo "$pdf" | awk '/^Pages:/ {print $2}')
    fonts=$(pdffonts "$pdf" | awk 'NR > 2 {print $1}' | sed 's/^[A-Z]*+//' | sort -u | tr '\n' ' ')
    echo "$pdf: $pages page(s); $fonts"
    [ "$pages" = 1 ] || { echo "make_cheatsheets: $pdf is $pages pages, not one"; fail=1; }
    case "$fonts" in *BarlowCondensed-Bold*) ;; *) echo "make_cheatsheets: $pdf without its fonts (offline?)"; fail=1;; esac
    if [ $png = 1 ]; then
        mkdir -p build/cheatsheets
        pdftoppm -r 110 -png -singlefile "$pdf" "build/cheatsheets/$name"
    fi
done
exit $fail
