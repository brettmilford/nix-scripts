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
- On the very first line, output the date found in the document as DATE: YYYY-MM-DD (e.g. DATE: 2025-07-30). If no date is found, output DATE: NONE
- Then a blank line, then the markdown transcription
- Transcribe character-by-character; do not normalise or \"improve\" wording
- Preserve original phrasing even if it looks like a typo or odd word choice
- For any word you are less than confident about, mark it like [word?] inline, then re-read the image once more and revise any [word?] markers.
- Keep the structure (headings, the quoted line, paragraph breaks)
- Apart from the DATE line, output only the markdown, no preamble or commentary" "${page_args[@]}" > "$tmpdir/output.md"

date_line="$(head -1 "$tmpdir/output.md")"
tail -n +3 "$tmpdir/output.md" > "$tmpdir/body.md"

pandoc -f markdown-auto_identifiers -t org "$tmpdir/body.md" -o "$tmpdir/output.org"

uuid="$(uuidgen | tr '[:upper:]' '[:lower:]')"
pdf_dir="$(dirname "$pdf_abs")"

if [[ "$date_line" =~ ^DATE:\ ([0-9]{4}-[0-9]{2}-[0-9]{2})$ ]]; then
    doc_date="${BASH_REMATCH[1]}"
    long_date="$(date -j -f "%Y-%m-%d" "$doc_date" "+%A the %-d of %B %Y" 2>/dev/null || date -d "$doc_date" "+%A the %-d of %B %Y")"
    month="$(date -j -f "%Y-%m-%d" "$doc_date" "+%B" 2>/dev/null || date -d "$doc_date" "+%B")"
    year="$(date -j -f "%Y-%m-%d" "$doc_date" "+%Y" 2>/dev/null || date -d "$doc_date" "+%Y")"

    org_out="${pdf_dir}/${doc_date}.org"
    new_pdf="${pdf_dir}/${doc_date}.pdf"
    if [ "$pdf_abs" != "$new_pdf" ]; then
        mv "$pdf_abs" "$new_pdf"
    fi

    {
        echo ":PROPERTIES:"
        echo ":ID:       $uuid"
        echo ":END:"
        echo "#+title: ${long_date}"
        echo "#+filetags: :${year}:${month}:"
        echo ""
        cat "$tmpdir/output.org"
    } > "$org_out"
else
    {
        echo ":PROPERTIES:"
        echo ":ID:       $uuid"
        echo ":END:"
        echo ""
        cat "$tmpdir/output.org"
    } > "$org_out"
fi

echo "Wrote $org_out"
