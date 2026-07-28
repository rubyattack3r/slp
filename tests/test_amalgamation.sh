#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
read -r -a CC_COMMAND <<< "${CC:-cc}"
RUN_BINARIES="${SLP_AMALGAMATION_RUN:-1}"
read -r -a LINK_LIBRARIES <<< "${SLP_AMALGAMATION_LDLIBS:--lm}"
RUNNER_COMMAND=()
if [ -n "${SLP_AMALGAMATION_RUNNER:-}" ]; then
    read -r -a RUNNER_COMMAND <<< "$SLP_AMALGAMATION_RUNNER"
fi
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

run_executable() {
    local executable="$1"
    if [ "$RUN_BINARIES" = "1" ]; then
        "$executable"
    elif [ "${#RUNNER_COMMAND[@]}" -gt 0 ]; then
        "${RUNNER_COMMAND[@]}" "$executable"
    fi
}

assert_output() {
    local executable="$1"
    local output

    if [ "$RUN_BINARIES" != "1" ] &&
       [ "${#RUNNER_COMMAND[@]}" -eq 0 ]; then
        return
    fi
    output="$(run_executable "$executable")"
    output="${output//$'\r'/}"
    if [ "$output" != "amalgamation ok" ]; then
        echo "Unexpected amalgamation output: $output" >&2
        exit 1
    fi
}

"$ROOT_DIR/scripts/amalgamate.sh"

"${CC_COMMAND[@]}" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/core_consumer.c" \
    "$ROOT_DIR/dist/slp.c" "${LINK_LIBRARIES[@]}" \
    -o "$TEMP_DIR/core-two-file"
if [ "$RUN_BINARIES" = "1" ] ||
   [ "${#RUNNER_COMMAND[@]}" -gt 0 ]; then
    run_executable "$TEMP_DIR/core-two-file"
fi

"${CC_COMMAND[@]}" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/stdlib_consumer.c" \
    "$ROOT_DIR/dist/slp.c" \
    "$ROOT_DIR/dist/slp_stdlib.c" "${LINK_LIBRARIES[@]}" \
    -o "$TEMP_DIR/stdlib-two-file"
assert_output "$TEMP_DIR/stdlib-two-file"

"$ROOT_DIR/scripts/amalgamate.sh" --single

cat > "$TEMP_DIR/core_implementation.c" <<'EOF'
#define SLP_IMPLEMENTATION
#include "slp.h"
EOF

"${CC_COMMAND[@]}" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/core_consumer.c" \
    "$TEMP_DIR/core_implementation.c" "${LINK_LIBRARIES[@]}" \
    -o "$TEMP_DIR/core-single-header"
if [ "$RUN_BINARIES" = "1" ] ||
   [ "${#RUNNER_COMMAND[@]}" -gt 0 ]; then
    run_executable "$TEMP_DIR/core-single-header"
fi

cat > "$TEMP_DIR/full_implementation.c" <<'EOF'
#define SLP_IMPLEMENTATION
#include "slp.h"

#define SLP_STDLIB_IMPLEMENTATION
#include "slp_stdlib.h"
EOF

"${CC_COMMAND[@]}" -std=c99 -I"$ROOT_DIR/dist" \
    "$TEMP_DIR/stdlib_consumer.c" \
    "$TEMP_DIR/full_implementation.c" "${LINK_LIBRARIES[@]}" \
    -o "$TEMP_DIR/stdlib-single-header"
assert_output "$TEMP_DIR/stdlib-single-header"

# Leave dist/ in the default two-file form expected by release packaging.
"$ROOT_DIR/scripts/amalgamate.sh"

if [ "$RUN_BINARIES" = "1" ] ||
   [ "${#RUNNER_COMMAND[@]}" -gt 0 ]; then
    echo "Amalgamation tests passed."
else
    echo "Amalgamation compile and link checks passed."
fi
