#!/usr/bin/env bash
# bundle.sh - CI/CD wrapper for cna_bundler
# Usage: ./bundle.sh <projects-directory> <output-file>

set -e

PROJECTS_DIR="${1:-examples/cna-projects}"
OUTPUT_FILE="${2:-bundle.cna}"
CONFIG_FILE="${3}"

# Ensure output parent directory exists
mkdir -p "$(dirname "$OUTPUT_FILE")"

echo "========================================"
echo " Building cna_bundler..."
echo "========================================"
# Ensure we build inside the nix environment to guarantee dependencies are met
nix develop -c make cna_bundler

if [ ! -f "./bin/cna_bundler" ]; then
    echo "[!] Build failed: cna_bundler executable not found."
    exit 1
fi

echo ""
echo "========================================"
echo " Bundling projects in: $PROJECTS_DIR"
echo "========================================"
# Run the bundler
if [ -n "$CONFIG_FILE" ]; then
    ./bin/cna_bundler "$PROJECTS_DIR" -o "$OUTPUT_FILE" --config "$CONFIG_FILE"
else
    ./bin/cna_bundler "$PROJECTS_DIR" -o "$OUTPUT_FILE"
fi

if [ $? -eq 0 ] && [ -f "$OUTPUT_FILE" ]; then
    echo ""
    echo "========================================"
    echo " Success! Bundle created at: $OUTPUT_FILE"
    echo " File size: $(wc -c < "$OUTPUT_FILE" | tr -d ' ') bytes"
    echo " Total Aliases: $(grep -c "alias " "$OUTPUT_FILE" || echo 0)"
    echo "========================================"
else
    echo "[!] Bundling failed or output file was not created."
    exit 1
fi
