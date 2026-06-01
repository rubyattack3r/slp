#!/bin/bash
# amalgamate.sh - Creates a single slp.h and slp.c from the project sources

set -e

# Ensure we are in the scripts directory so relative paths work
cd "$(dirname "$0")"

OUT_DIR="../dist"
mkdir -p "$OUT_DIR"
mkdir -p "$OUT_DIR/platform"
cp ../src/platform/*.c "$OUT_DIR/platform/"

SINGLE_HEADER=0
if [ "$1" == "--single" ]; then
    SINGLE_HEADER=1
    echo "Single header mode enabled."
fi

HEADER_OUT="$OUT_DIR/slp.h"
SOURCE_OUT="$OUT_DIR/slp.c"

echo "Amalgamating headers into $HEADER_OUT..."
cat << 'EOF' > "$HEADER_OUT"
/*
 * SLP Amalgamated Header
 * Generated automatically.
 */
#ifndef SLP_H
#define SLP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

EOF

# Add headers in dependency order. Filter out #include "..."
for f in ../include/slp_common.h ../include/slp_core.h ../include/slp_platform.h ../include/slp_utils.h ../include/slp_lexer.h ../include/slp_parser.h ../include/slp_ast.h ../include/slp_opcodes.h ../include/slp_value.h ../include/slp_chunk.h ../include/slp_gc.h ../include/slp_vm.h ../include/slp_compiler.h ../include/slp_disasm.h ../include/slp_bytecode.h; do
    echo "/* --- File: $f --- */" >> "$HEADER_OUT"
    # Remove #pragma once, #ifndef/#define guards at the top, and #include "..." local headers
    cat "$f" | egrep -v '^\s*#include\s+"' | egrep -v '^\s*#ifndef\s+SLP_.*_H\s*$' | egrep -v '^\s*#define\s+SLP_.*_H\s*$' | python3 -c "import sys, re; lines = sys.stdin.readlines();
while lines and lines[-1].strip() == '': lines.pop();
if lines and re.match(r'^\s*#endif.*', lines[-1]): lines.pop();
sys.stdout.write(''.join(lines))" >> "$HEADER_OUT"
    echo "" >> "$HEADER_OUT"
done

if [ "$SINGLE_HEADER" -eq 0 ]; then
    echo "#endif // SLP_H" >> "$HEADER_OUT"
fi

if [ "$SINGLE_HEADER" -eq 0 ]; then
    echo "Amalgamating sources into $SOURCE_OUT..."
    cat << 'EOF' > "$SOURCE_OUT"
/*
 * SLP Amalgamated Source
 * Generated automatically.
 */
#include "slp.h"

EOF
fi

for f in ../src/slp_utils.c ../src/slp_lexer.c ../src/slp_parser.c ../src/slp_ast.c ../src/slp_value.c ../src/slp_chunk.c ../src/slp_gc.c ../src/slp_vm.c ../src/slp_compiler.c ../src/slp_disasm.c ../src/slp_bytecode.c ../src/slp_platform.c; do
    if [ "$SINGLE_HEADER" -eq 1 ]; then
        echo "/* --- File: $f --- */" >> "$HEADER_OUT"
        cat "$f" | egrep -v '^\s*#include\s+"' >> "$HEADER_OUT"
        echo "" >> "$HEADER_OUT"
    else
        echo "/* --- File: $f --- */" >> "$SOURCE_OUT"
        cat "$f" | egrep -v '^\s*#include\s+"' >> "$SOURCE_OUT"
        echo "" >> "$SOURCE_OUT"
    fi
done

if [ "$SINGLE_HEADER" -eq 1 ]; then
    # Close the inclusion guard if we combined everything
    echo "#endif // SLP_H" >> "$HEADER_OUT"
    echo "Single header amalgamation complete: $HEADER_OUT"
else
    echo "Amalgamation complete."
fi
