#!/bin/bash

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $(basename "$0") <pdf-file>"
    exit 1
fi

pdf="$1"

if [ ! -f "$pdf" ]; then
    echo "Error: '$pdf' does not exist"
    exit 1
fi

pdf_abs="$(cd "$(dirname "$pdf")" && pwd)/$(basename "$pdf")"
basename_no_ext="${pdf_abs%.pdf}"
org_out="${basename_no_ext}.org"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

pdftoppm -png "$pdf_abs" "$tmpdir/page"

page_args=()
for img in "$tmpdir"/page-*.png; do
    page_args+=("$img")
done

cd "$tmpdir"
claude -p --model opus "Ultrathink. Carefully OCR the handwritten images to markdown.

Rules:
- Transcribe character-by-character; do not normalise or \"improve\" wording
- Preserve original phrasing even if it looks like a typo or odd word choice
- For any word you are less than confident about, mark it like [word?] inline, then re-read the image once more and revise any [word?] markers.
- Keep the structure (headings, the quoted line, paragraph breaks)
- Output only the markdown, no preamble or commentary" "${page_args[@]}" > "$tmpdir/output.md"

pandoc -f markdown-auto_identifiers -t org "$tmpdir/output.md" -o "$tmpdir/output.org"

uuid="$(uuidgen | tr '[:upper:]' '[:lower:]')"

{
    echo ":PROPERTIES:"
    echo ":ID:       $uuid"
    echo ":END:"
    echo ""
    cat "$tmpdir/output.org"
} > "$org_out"

echo "Wrote $org_out"
