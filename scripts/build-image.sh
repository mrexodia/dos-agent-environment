#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
INPUTS="$ROOT/inputs"
GUEST="$ROOT/guest"
PAYLOAD="$ROOT/payload"
BUILD="$ROOT/build"
OUT="$BUILD/dos71.img"
# 1024 cylinders * 16 heads * 63 sectors * 512 bytes = exactly 504 MiB.
# The matching geometry is declared explicitly to QEMU in harness/dosvm.py.
DISK_SIZE_MIB=504

required=(Windows98_SE_No_Ramdrive.img pcntpk.com cwsdpmi.exe \
          mTCP_2025-01-10_upx.zip links-2.30.exe)
for name in "${required[@]}"; do
    if [[ ! -f "$INPUTS/$name" ]]; then
        echo "missing required input: inputs/$name (see inputs/README.md)" >&2
        exit 2
    fi
    if ! grep -Eq "^[0-9a-fA-F]{64}[[:space:]]+[* ]?$name$" "$INPUTS/SHA256SUMS"; then
        echo "missing approved checksum for inputs/$name in inputs/SHA256SUMS" >&2
        exit 2
    fi
done

(
    cd "$INPUTS"
    sha256sum --check SHA256SUMS
)

for file in "$GUEST/MSDOS.SYS" "$GUEST/CONFIG.SYS" \
            "$GUEST/AUTOEXEC.BAT" "$GUEST/BIN/SERIAL.BAT" \
            "$GUEST/MTCP/TCP.CFG"; do
    python3 - "$file" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
data = p.read_bytes()
if b'\n' in data.replace(b'\r\n', b''):
    raise SystemExit(f"guest text file is not CRLF-only: {p}")
PY
done

mkdir -p "$BUILD"
work=$(mktemp -d "$BUILD/.image.XXXXXX")
trap 'rm -rf "$work"' EXIT

disk="$work/dos71.img"
part="$work/dos71.part"
floppy="$INPUTS/Windows98_SE_No_Ramdrive.img"

truncate -s "${DISK_SIZE_MIB}MiB" "$disk"
parted -s "$disk" \
    mklabel msdos \
    mkpart primary fat16 1MiB 100% \
    set 1 boot on

part_line=$(parted -m -s "$disk" unit s print | awk -F: '$1 == "1" { print; exit }')
if [[ -z "$part_line" ]]; then
    echo "failed to read partition geometry" >&2
    exit 1
fi
IFS=: read -r _ start end length _ <<<"$part_line"
start=${start%s}
length=${length%s}
if [[ ! "$start" =~ ^[0-9]+$ || ! "$length" =~ ^[0-9]+$ ]]; then
    echo "unexpected partition geometry: $part_line" >&2
    exit 1
fi

truncate -s "$((length * 512))" "$part"
mformat -i "$part" -h 16 -n 63 -H "$start" -v DOS71 ::
ms-sys --force --fat16 "$part"
# mformat treats a standalone partition image like a floppy and writes drive
# number 0x00. This partition will be C:, so correct the FAT16 BPB field.
printf '\x80' | dd of="$part" bs=1 seek=36 conv=notrunc status=none

mkdir -p "$work/system"
mcopy -i "$floppy" ::/IO.SYS "$work/system/IO.SYS"
mcopy -i "$floppy" ::/COMMAND.COM "$work/system/COMMAND.COM"

# IO.SYS must be the first file allocated on the freshly formatted filesystem.
mcopy -i "$part" "$work/system/IO.SYS" ::/IO.SYS
mcopy -i "$part" "$GUEST/MSDOS.SYS" ::/MSDOS.SYS
mcopy -i "$part" "$work/system/COMMAND.COM" ::/COMMAND.COM
mattrib -i "$part" +s +h +r ::/IO.SYS
mattrib -i "$part" +s +h +r ::/MSDOS.SYS

for dir in DOS MTCP DRIVERS BIN TMP; do
    mmd -i "$part" "::/$dir"
done

mcopy -i "$part" "$GUEST/CONFIG.SYS" ::/CONFIG.SYS
mcopy -i "$part" "$GUEST/AUTOEXEC.BAT" ::/AUTOEXEC.BAT
mcopy -i "$part" "$GUEST/BIN/SERIAL.BAT" ::/BIN/SERIAL.BAT
mcopy -i "$part" "$GUEST/MTCP/TCP.CFG" ::/MTCP/TCP.CFG
mcopy -i "$part" "$INPUTS/pcntpk.com" ::/DRIVERS/PCNTPK.COM
mcopy -i "$part" "$INPUTS/cwsdpmi.exe" ::/BIN/CWSDPMI.EXE

# A useful, deliberately small subset of the supplied boot-floppy tools.
for name in HIMEM.SYS MEM.EXE EDIT.COM EDIT.HLP ATTRIB.EXE CHKDSK.EXE \
            DELTREE.EXE MOVE.EXE SYS.COM; do
    if mdir -i "$floppy" "::/$name" >/dev/null 2>&1; then
        mcopy -i "$floppy" "::/$name" "$work/system/$name"
        mcopy -i "$part" "$work/system/$name" "::/DOS/$name"
    fi
done

# Deploy the pinned mTCP executables required by networking and collection.
mtcp_archive="$INPUTS/mTCP_2025-01-10_upx.zip"
mkdir -p "$work/mtcp"
unzip -q "$mtcp_archive" -d "$work/mtcp"
while IFS= read -r -d '' file; do
    base=$(basename "$file" | tr '[:lower:]' '[:upper:]')
    mcopy -o -i "$part" "$file" "::/MTCP/$base"
done < <(find "$work/mtcp" -type f \( -iname '*.exe' -o -iname '*.com' \) -print0)

# Copy each payload top-level entry; an empty payload directory is valid.
shopt -s nullglob dotglob
for entry in "$PAYLOAD"/*; do
    mcopy -o -s -i "$part" "$entry" ::/
done
shopt -u nullglob dotglob

fsck.fat -vn "$part"
mdir -a -i "$part" ::

dd if="$part" of="$disk" bs=512 seek="$start" conv=notrunc,sparse status=none
ms-sys --force --mbr95b "$disk"

# work/ is under build/, so this rename is atomic on the same filesystem.
mv -f "$disk" "$OUT"
echo "built $OUT (${DISK_SIZE_MIB} MiB, partition starts at sector $start)"
