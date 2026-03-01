#!/bin/bash
# amalgamate.sh - Creates a single sleepy.h and sleepy.c from the project sources

set -e

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
for f in ../include/sleepy_common.h ../include/sleepy_core.h ../include/sleepy_utils.h ../include/sleepy_lexer.h ../include/sleepy_parser.h; do
    echo "/* --- File: $f --- */" >> "$HEADER_OUT"
    # Remove #pragma once, #ifndef/#define guards at the top, and #include "..." local headers
    cat "$f" | egrep -v '^\s*#include\s+"' | egrep -v '^\s*#ifndef\s+SLEEPY_.*_H\s*$' | egrep -v '^\s*#define\s+SLEEPY_.*_H\s*$' | egrep -v '^\s*#endif\s*//\s*SLEEPY_.*_H\s*$' >> "$HEADER_OUT"
    echo "" >> "$HEADER_OUT"
done

if [ "$SINGLE_HEADER" -eq 0 ]; then
    echo "#endif // SLEEPY_H" >> "$HEADER_OUT"
fi

	echo "Amalgamating sources into $SOURCE_OUT..."
	cat << 'EOF' > "$SOURCE_OUT"
/* 
 * Sleepy Amalgamated Source
 * Generated automatically.
 */
#include "sleepy.h"

EOF
fi

for f in ../src/sleepy_utils.c ../src/sleepy_lexer.c ../src/sleepy_parser.c; do
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
