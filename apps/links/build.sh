#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source_binary="$ROOT/inputs/links-2.30.exe"
if [[ ! -f "$source_binary" ]]; then
    echo "missing inputs/links-2.30.exe; see inputs/README.md" >&2
    exit 2
fi
mkdir -p "$ROOT/payload/BIN"
cp "$source_binary" "$ROOT/payload/BIN/LINKS.EXE"
echo "deployed payload/BIN/LINKS.EXE"
