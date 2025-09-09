#!/bin/bash

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $(basename "$0") <directory1> <directory2>"
    echo "Example: $(basename "$0") /path/to/dir1 /path/to/dir2"
    exit 1
fi

DIR1="$1"
DIR2="$2"

# Check if directories exist
if [ ! -d "$DIR1" ]; then
    echo "Error: Directory '$DIR1' does not exist"
    exit 1
fi

if [ ! -d "$DIR2" ]; then
    echo "Error: Directory '$DIR2' does not exist"
    exit 1
fi

# Create temporary files
TEMP_DIR=$(mktemp -d)
DIR1_CHECKSUMS="$TEMP_DIR/dir1_checksums.txt"
DIR2_CHECKSUMS="$TEMP_DIR/dir2_checksums.txt"
DIR1_HASHES="$TEMP_DIR/dir1_hashes.txt"
DIR2_HASHES="$TEMP_DIR/dir2_hashes.txt"
UNIQUE_TO_DIR1="$TEMP_DIR/unique_to_dir1.txt"
UNIQUE_TO_DIR2="$TEMP_DIR/unique_to_dir2.txt"

# Cleanup function
cleanup() {
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

echo "Comparing directory trees:"
echo "  Directory 1: $DIR1"
echo "  Directory 2: $DIR2"
echo

# Count files in each directory
echo "Counting files..."
COUNT1=$(find "$DIR1" -type f | wc -l)
COUNT2=$(find "$DIR2" -type f | wc -l)
echo "  $DIR1: $COUNT1 files"
echo "  $DIR2: $COUNT2 files"
echo

# Generate checksums
echo "Generating checksums for directory 1..."
find "$DIR1" -type f -exec md5sum {} \; | sort > "$DIR1_CHECKSUMS"

echo "Generating checksums for directory 2..."
find "$DIR2" -type f -exec md5sum {} \; | sort > "$DIR2_CHECKSUMS"

# Extract just the hash values (ignore file paths)
cut -d' ' -f1 "$DIR1_CHECKSUMS" | sort > "$DIR1_HASHES"
cut -d' ' -f1 "$DIR2_CHECKSUMS" | sort > "$DIR2_HASHES"

echo "Comparing checksums..."
echo

# Compare the hash lists
if diff "$DIR1_HASHES" "$DIR2_HASHES" > /dev/null; then
    echo "[SUCCESS] Directories contain identical files (by content)"

    # Check if file counts match
    if [ "$COUNT1" -eq "$COUNT2" ]; then
        echo "[SUCCESS] File counts also match ($COUNT1 files each)"
    else
        echo "[WARNING] File counts differ but content hashes are identical"
        echo "          This could mean duplicate files exist in one directory"
    fi
else
    echo "[DIFFERENCE] Directories contain different files"
    echo

    # Find unique hashes and their corresponding files
    comm -23 "$DIR1_HASHES" "$DIR2_HASHES" > "$UNIQUE_TO_DIR1"
    comm -13 "$DIR1_HASHES" "$DIR2_HASHES" > "$UNIQUE_TO_DIR2"

    UNIQUE_COUNT_1=$(wc -l < "$UNIQUE_TO_DIR1")
    UNIQUE_COUNT_2=$(wc -l < "$UNIQUE_TO_DIR2")

    if [ "$UNIQUE_COUNT_1" -gt 0 ]; then
        echo "Files unique to $DIR1: ($UNIQUE_COUNT_1 files)"

        if [ "$UNIQUE_COUNT_1" -le 10 ]; then
            # Show all files inline
            while read -r hash; do
                grep "^$hash " "$DIR1_CHECKSUMS" | cut -d' ' -f2- | sed 's|^|  |'
            done < "$UNIQUE_TO_DIR1"
        else
            # Show first 5 files and save full list to local file
            count=0
            while read -r hash && [ "$count" -lt 5 ]; do
                grep "^$hash " "$DIR1_CHECKSUMS" | cut -d' ' -f2- | sed 's|^|  |'
                count=$((count + 1))
            done < "$UNIQUE_TO_DIR1"

            # Save full list to local file
            OUTPUT_FILE="unique_to_$(basename "$DIR1")_$(date +%Y%m%d_%H%M%S).txt"
            echo "  ... and $((UNIQUE_COUNT_1 - 5)) more files"
            echo "  Full list saved to: $OUTPUT_FILE"

            while read -r hash; do
                grep "^$hash " "$DIR1_CHECKSUMS" | cut -d' ' -f2-
            done < "$UNIQUE_TO_DIR1" > "$OUTPUT_FILE"
        fi
        echo
    fi

    if [ "$UNIQUE_COUNT_2" -gt 0 ]; then
        echo "Files unique to $DIR2: ($UNIQUE_COUNT_2 files)"

        if [ "$UNIQUE_COUNT_2" -le 10 ]; then
            # Show all files inline
            while read -r hash; do
                grep "^$hash " "$DIR2_CHECKSUMS" | cut -d' ' -f2- | sed 's|^|  |'
            done < "$UNIQUE_TO_DIR2"
        else
            # Show first 5 files and save full list to local file
            count=0
            while read -r hash && [ "$count" -lt 5 ]; do
                grep "^$hash " "$DIR2_CHECKSUMS" | cut -d' ' -f2- | sed 's|^|  |'
                count=$((count + 1))
            done < "$UNIQUE_TO_DIR2"

            # Save full list to local file
            OUTPUT_FILE="unique_to_$(basename "$DIR2")_$(date +%Y%m%d_%H%M%S).txt"
            echo "  ... and $((UNIQUE_COUNT_2 - 5)) more files"
            echo "  Full list saved to: $OUTPUT_FILE"

            while read -r hash; do
                grep "^$hash " "$DIR2_CHECKSUMS" | cut -d' ' -f2-
            done < "$UNIQUE_TO_DIR2" > "$OUTPUT_FILE"
        fi
        echo
    fi

    echo "For detailed diff of all checksums, run:"
    echo "  diff '$DIR1_CHECKSUMS' '$DIR2_CHECKSUMS'"
fi

echo
echo "Comparison complete."
