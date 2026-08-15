#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD="$ROOT/build"
RAW="$BUILD/dos71.img"
OUT="$BUILD/dos71.qcow2"

for pidfile in "$BUILD"/runs/*/qemu.pid; do
    [[ -e "$pidfile" ]] || continue
    pid=$(cat "$pidfile" 2>/dev/null || true)
    if [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null; then
        command=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)
        run_dir=$(dirname "$pidfile")
        if [[ "$command" == *qemu-system* && "$command" == *"$run_dir/disk.qcow2"* ]]; then
            echo "active DOS VM pid $pid; run './dosctl stop --all' first" >&2
            exit 2
        fi
    fi
done

"$ROOT/scripts/build-image.sh"
qemu-img convert -f raw -O qcow2 "$RAW" "$OUT.tmp"
mv -f "$OUT.tmp" "$OUT"
qemu-img info "$OUT"
echo "built $OUT"
