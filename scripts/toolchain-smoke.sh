#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT="$ROOT/build/toolchains"
PAYLOAD="$ROOT/payload/BIN"
mkdir -p "$OUT" "$PAYLOAD"
rm -f "$OUT"/TC*.COM "$OUT"/TC*.EXE "$OUT"/TC*.exe \
    "$OUT"/*.o "$OUT"/*.a "$OUT"/djgpp-output.exe

required=(
    dos-cc32-djgpp dos-cc16-gcc dos-cc16-watcom dos-cc32-watcom
    dos-pas16 dos-pas32 dos-asm-nasm dos-asm-masm dos-asm-fasm bcc upx
)
for tool in "${required[@]}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "missing optional toolchain command: $tool" >&2
        echo "use .devcontainer-toolchains/devcontainer.json" >&2
        exit 2
    fi
done

dos-cc32-djgpp -Os -s -o "$OUT/djgpp-output.exe" \
    "$ROOT/toolchains/hello/djgpp.c"
mv "$OUT/djgpp-output.exe" "$OUT/TCDJGPP.EXE"
dos-cc16-gcc -Os -s -o "$OUT/TCIA16.EXE" \
    "$ROOT/toolchains/hello/ia16.c"
dos-asm-masm -q -bin -Fo"$OUT/TCJWASM.COM" \
    "$ROOT/toolchains/hello/jwasm.asm"
dos-asm-nasm -f bin -o "$OUT/TCNASM.COM" \
    "$ROOT/toolchains/hello/nasm.asm"
bcc -Md -ansi -O -o "$OUT/TCBCC.COM" "$ROOT/toolchains/hello/bcc.c"
dos-pas32 -FE"$OUT" -oTCFPC.EXE "$ROOT/toolchains/hello/fpc.pas"

dos-cc16-watcom -fo="$OUT/TCW16.o" -fe="$OUT/TCW16.EXE" \
    "$ROOT/toolchains/hello/watcom16.c"
dos-cc16-watcom -bcl=com -fo="$OUT/TCWCOM.o" -fe="$OUT/TCWCOM.COM" \
    "$ROOT/toolchains/hello/watcomcom.c"
dos-cc16-watcom -fo="$OUT/TCWCPP.o" -fe="$OUT/TCWCPP.EXE" \
    "$ROOT/toolchains/hello/watcom.cpp"
dos-cc32-watcom -fo="$OUT/TCW32.o" -fe="$OUT/TCW32.EXE" \
    "$ROOT/toolchains/hello/watcom32.c"
dos-pas16 -FE"$OUT" -oTCFPC16.EXE "$ROOT/toolchains/hello/fpc16.pas"
dos-asm-fasm "$ROOT/toolchains/hello/fasm.asm" "$OUT/TCFASM.COM"

cp "$OUT/TCDJGPP.EXE" "$OUT/TCUPX.EXE"
upx --best --quiet "$OUT/TCUPX.EXE"

cp "$OUT"/TC*.EXE "$OUT"/TC*.COM "$PAYLOAD"/
cp /opt/watcom/binw/dos4gw.exe "$PAYLOAD/DOS4GW.EXE"
"$ROOT/scripts/build-runtime.sh"

PYTHONPATH="$ROOT" python3 - <<'PY'
from harness.dosvm import DosVM

expected = {
    "TCDJGPP.EXE": "DOS_AGENT_DJGPP",
    "TCIA16.EXE": "DOS_AGENT_IA16",
    "TCJWASM.COM": "DOS_AGENT_JWASM",
    "TCNASM.COM": "DOS_AGENT_NASM",
    "TCBCC.COM": "DOS_AGENT_BCC",
    "TCFPC.EXE": "DOS_AGENT_FPC",
    "TCFPC16.EXE": "DOS_AGENT_FPC16",
    "TCFASM.COM": "DOS_AGENT_FASM",
    "TCW16.EXE": "DOS_AGENT_WATCOM16",
    "TCWCOM.COM": "DOS_AGENT_WATCOM_COM",
    "TCWCPP.EXE": "DOS_AGENT_WATCOM_CPP",
    "TCW32.EXE": "DOS_AGENT_WATCOM32",
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
