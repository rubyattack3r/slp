#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: JAVA_HOME=/path/to/jdk $0 OUTPUT_DIR" >&2
    exit 2
fi

if [[ -z "${JAVA_HOME:-}" ]]; then
    echo "JAVA_HOME is required" >&2
    exit 2
fi
if [[ -n "${SLP_SLEEP_JAR:-}" && ! -f "$SLP_SLEEP_JAR" ]]; then
    echo "Sleep runtime not found: $SLP_SLEEP_JAR" >&2
    exit 2
fi

output_dir=$1
if [[ -e "$output_dir" ]]; then
    echo "refusing to replace existing output: $output_dir" >&2
    exit 2
fi

make jvm JAVA_HOME="$JAVA_HOME"
mkdir -p "$output_dir"
"$JAVA_HOME/bin/jlink" \
    --module-path "$JAVA_HOME/jmods" \
    --add-modules "${SLP_JVM_MODULES:-java.se,jdk.unsupported,jdk.crypto.ec}" \
    --strip-debug \
    --no-header-files \
    --no-man-pages \
    --output "$output_dir/runtime"
cp bin/slp-jvm-bridge.jar \
    "$output_dir/runtime/lib/slp-jvm-bridge.jar"
if [[ -n "${SLP_SLEEP_JAR:-}" ]]; then
    cp "$SLP_SLEEP_JAR" "$output_dir/runtime/lib/sleep.jar"
fi

echo "packaged JVM runtime: $output_dir/runtime"
