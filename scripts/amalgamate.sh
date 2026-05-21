#!/bin/bash
# amalgamate.sh - Creates a single sleepy.h and sleepy.c from the project sources

set -e

# Ensure we are in the scripts directory so relative paths work
cd "$(dirname "$0")"

OUT_DIR="../dist"
mkdir -p "$OUT_DIR"

SINGLE_HEADER=0
if [ "$1" == "--single" ]; then
    SINGLE_HEADER=1
    echo "Single header mode enabled."
fi

HEADER_OUT="$OUT_DIR/sleepy.h"
SOURCE_OUT="$OUT_DIR/sleepy.c"

echo "Amalgamating headers into $HEADER_OUT..."
cat << 'EOF' > "$HEADER_OUT"
/*
 * Sleepy Amalgamated Header
 * Generated automatically.
 */
#ifndef SLEEPY_H
#define SLEEPY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

EOF

# Add headers in dependency order. Filter out #include "..."
for f in ../include/sleepy_common.h ../include/sleepy_core.h ../include/sleepy_utils.h ../include/sleepy_lexer.h ../include/sleepy_parser.h ../include/sleepy_ast.h ../include/sleepy_opcodes.h ../include/sleepy_value.h ../include/sleepy_chunk.h ../include/sleepy_gc.h ../include/sleepy_vm.h ../include/sleepy_compiler.h ../include/sleepy_disasm.h ../include/sleepy_bytecode.h; do
    echo "/* --- File: $f --- */" >> "$HEADER_OUT"
    # Remove #pragma once, #ifndef/#define guards at the top, and #include "..." local headers
    cat "$f" | egrep -v '^\s*#include\s+"' | egrep -v '^\s*#ifndef\s+SLEEPY_.*_H\s*$' | egrep -v '^\s*#define\s+SLEEPY_.*_H\s*$' | python3 -c "import sys, re; lines = sys.stdin.readlines();
while lines and lines[-1].strip() == '': lines.pop();
if lines and re.match(r'^\s*#endif.*', lines[-1]): lines.pop();
sys.stdout.write(''.join(lines))" >> "$HEADER_OUT"
    echo "" >> "$HEADER_OUT"
done

if [ "$SINGLE_HEADER" -eq 0 ]; then
    echo "#endif // SLEEPY_H" >> "$HEADER_OUT"
fi

if [ "$SINGLE_HEADER" -eq 0 ]; then
    echo "Amalgamating sources into $SOURCE_OUT..."
    cat << 'EOF' > "$SOURCE_OUT"
/*
 * Sleepy Amalgamated Source
 * Generated automatically.
 */
#include "sleepy.h"

EOF
fi

for f in ../src/sleepy_utils.c ../src/sleepy_lexer.c ../src/sleepy_parser.c ../src/sleepy_ast.c ../src/sleepy_value.c ../src/sleepy_chunk.c ../src/sleepy_gc.c ../src/sleepy_vm.c ../src/sleepy_compiler.c ../src/sleepy_disasm.c ../src/sleepy_bytecode.c; do
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
    echo "#endif // SLEEPY_H" >> "$HEADER_OUT"
    echo "Single header amalgamation complete: $HEADER_OUT"
else
    echo "Amalgamation complete."
fi
