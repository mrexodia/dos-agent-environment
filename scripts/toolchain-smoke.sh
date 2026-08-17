#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT="$ROOT/build/toolchains"
PAYLOAD="$ROOT/payload/BIN"
mkdir -p "$OUT" "$PAYLOAD"
rm -f "$OUT"/TC*.COM "$OUT"/TC*.EXE "$OUT"/TC*.exe "$OUT"/djgpp-output.exe

DJGPP_GCC=${DJGPP_GCC:-/opt/djgpp/bin/i586-pc-msdosdjgpp-gcc}
IA16_GCC=${IA16_GCC:-/opt/ia16/bin/ia16-elf-gcc}
FPC=${FPC:-/opt/fpc/bin/ppcross386}

for tool in "$DJGPP_GCC" "$IA16_GCC" "$FPC" jwasm bcc upx; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "missing optional toolchain command: $tool" >&2
        echo "use .devcontainer-toolchains/devcontainer.json" >&2
        exit 2
    fi
done

"$DJGPP_GCC" -Os -s -o "$OUT/djgpp-output.exe" "$ROOT/toolchains/hello/djgpp.c"
mv "$OUT/djgpp-output.exe" "$OUT/TCDJGPP.EXE"
"$IA16_GCC" -Os -s -mcmodel=medium -o "$OUT/TCIA16.EXE" \
    "$ROOT/toolchains/hello/ia16.c" -li86
jwasm -q -bin -Fo"$OUT/TCJWASM.COM" "$ROOT/toolchains/hello/jwasm.asm"
bcc -Md -ansi -O -o "$OUT/TCBCC.COM" "$ROOT/toolchains/hello/bcc.c"

fpc_units=$(find /opt/fpc -type d -path '*/units/go32v2/rtl' | head -n 1)
if [[ -z "$fpc_units" ]]; then
    echo "Free Pascal GO32v2 RTL units were not installed" >&2
    exit 2
fi
"$FPC" -n -Tgo32v2 -Fu"$fpc_units" -FE"$OUT" -oTCFPC.EXE \
    "$ROOT/toolchains/hello/fpc.pas"

cp "$OUT/TCDJGPP.EXE" "$OUT/TCUPX.EXE"
upx --best --quiet "$OUT/TCUPX.EXE"

cp "$OUT"/TC*.EXE "$OUT"/TC*.COM "$PAYLOAD"/
"$ROOT/scripts/build-runtime.sh"

PYTHONPATH="$ROOT" python3 - <<'PY'
from harness.dosvm import DosVM

expected = {
    "TCDJGPP.EXE": "DOS_AGENT_DJGPP",
    "TCIA16.EXE": "DOS_AGENT_IA16",
    "TCJWASM.COM": "DOS_AGENT_JWASM",
    "TCBCC.COM": "DOS_AGENT_BCC",
    "TCFPC.EXE": "DOS_AGENT_FPC",
    "TCUPX.EXE": "DOS_AGENT_DJGPP",
}
vm = DosVM.start(timeout=30.0)
try:
    for program, marker in expected.items():
        output = vm.exec(program, timeout=30.0)
        assert marker in output, f"{program}: {output!r}"
        print(f"{program}: PASS")
finally:
    vm.stop(force=True)
PY

echo "all optional toolchain compile-and-run tests passed"
