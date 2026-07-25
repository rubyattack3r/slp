#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
CC_BIN="${CC:-cc}"
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/slp-amalgamation.XXXXXX")"

cleanup() {
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

cat > "$TEMP_DIR/core_consumer.c" <<'EOF'
#include "slp.h"

#include <stdlib.h>

static void *host_alloc(
    void *pointer, size_t size, void *user_data) {
    (void)user_data;
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    return realloc(pointer, size);
}

int main(void) {
    SlpAllocator allocator = {host_alloc, NULL};
    SlpVM *vm = slp_vm_new(&allocator);
    if (!vm)
        return 1;
    slp_vm_free(vm);
    return 0;
}
EOF

cat > "$TEMP_DIR/stdlib_consumer.c" <<'EOF'
#include "slp.h"
#include "slp_stdlib.h"

#include <stdlib.h>

static void *host_alloc(
    void *pointer, size_t size, void *user_data) {
    (void)user_data;
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    return realloc(pointer, size);
}

int main(void) {
    SlpAllocator allocator = {host_alloc, NULL};
    SlpVM *vm = slp_vm_new(&allocator);
    if (!vm)
        return 1;

    slp_stdlib_init(vm);
    SlpResult result =
        slp_vm_interpret(vm, "println('amalgamation ok');");
    slp_vm_free(vm);
    return result == SLP_OK ? 0 : 1;
}
EOF

assert_output() {
    local executable="$1"
    local output

    output="$("$executable")"
    if [ "$output" != "amalgamation ok" ]; then
        echo "Unexpected amalgamation output: $output" >&2
        exit 1
    fi
}

"$ROOT_DIR/scripts/amalgamate.sh"

"$CC_BIN" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/core_consumer.c" \
    "$ROOT_DIR/dist/slp.c" -lm \
    -o "$TEMP_DIR/core-two-file"
"$TEMP_DIR/core-two-file"

"$CC_BIN" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/stdlib_consumer.c" \
    "$ROOT_DIR/dist/slp.c" \
    "$ROOT_DIR/dist/slp_stdlib.c" -lm \
    -o "$TEMP_DIR/stdlib-two-file"
assert_output "$TEMP_DIR/stdlib-two-file"

"$ROOT_DIR/scripts/amalgamate.sh" --single

cat > "$TEMP_DIR/core_implementation.c" <<'EOF'
#define SLP_IMPLEMENTATION
#include "slp.h"
EOF

"$CC_BIN" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/core_consumer.c" \
    "$TEMP_DIR/core_implementation.c" -lm \
    -o "$TEMP_DIR/core-single-header"
"$TEMP_DIR/core-single-header"

cat > "$TEMP_DIR/full_implementation.c" <<'EOF'
#define SLP_IMPLEMENTATION
#include "slp.h"

#define SLP_STDLIB_IMPLEMENTATION
#include "slp_stdlib.h"
EOF

"$CC_BIN" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/stdlib_consumer.c" \
    "$TEMP_DIR/full_implementation.c" -lm \
    -o "$TEMP_DIR/stdlib-single-header"
assert_output "$TEMP_DIR/stdlib-single-header"

echo "Amalgamation tests passed."
