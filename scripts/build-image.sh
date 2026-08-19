#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PAYLOAD="$ROOT/payload"
BUILD="$ROOT/build"
BASE="$BUILD/dos71-base.img"
OUT="$BUILD/dos71.img"

# The Make target tracks every declared base input. Calling this script directly
# remains safe and still refreshes a missing or stale base.
make --no-print-directory -C "$ROOT" build/dos71-base.img

mkdir -p "$BUILD"
work=$(mktemp -d "$BUILD/.deploy.XXXXXX")
trap 'rm -rf "$work"' EXIT
disk="$work/dos71.img"

# Clone the sparse clean image, then change only this disposable deployment
# candidate. GNU cp uses reflinks where the backing filesystem supports them.
cp --reflink=auto --sparse=always "$BASE" "$disk"

part_line=$(parted -m -s "$disk" unit s print | awk -F: '$1 == "1" { print; exit }')
if [[ -z "$part_line" ]]; then
    echo "failed to read partition geometry from clean base" >&2
    exit 1
fi
IFS=: read -r _ start _ _ _ <<<"$part_line"
start=${start%s}
if [[ ! "$start" =~ ^[0-9]+$ ]]; then
    echo "unexpected partition geometry: $part_line" >&2
    exit 1
fi
partition="$disk@@$((start * 512))"

# Copy each payload top-level entry into the cloned FAT filesystem. The clean
# base and all active run overlays remain untouched.
shopt -s nullglob dotglob
for entry in "$PAYLOAD"/*; do
    mcopy -o -s -i "$partition" "$entry" ::/
done
shopt -u nullglob dotglob

mdir -a -i "$partition" ::
mv -f "$disk" "$OUT"
echo "deployed payload into $OUT from cached clean base"
