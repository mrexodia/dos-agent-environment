# DOS Agent Environment

A scriptable DOS 7.1 VM for coding agents. QEMU runs entirely under TCG, so the
same devcontainer works on Apple-silicon and x86 hosts.

See [`PLAN.md`](PLAN.md) for architecture and milestones.

## Current capabilities

- rootless, reproducible FAT16 hard-disk construction from a user-supplied DOS
  boot floppy;
- disposable qcow2 overlays for every run;
- QMP keyboard input and CP437 VGA text capture;
- PNG screenshots with no graphical display attached;
- reliable non-interactive DOS command capture by temporarily switching CTTY to
  a private serial socket;
- a stable `python3 -m harness.dosctl` command-line interface;
- NASM `.COM` payload smoke test.

Networking and Links support activate when the optional
`mTCP_2025-01-10_upx.zip` and `links-2.30.exe` inputs are supplied. Both are
FOSS; the generated DOS image remains non-redistributable because it contains
Microsoft Windows 98 system files.

## Setup

Open the repository in its devcontainer, then run:

```bash
make runtime
make smoke
```

For a one-shot Docker verification without VS Code:

```bash
docker build -t dos-agent-dev -f .devcontainer/Dockerfile .
docker run --rm --init -v "$PWD:/workspace" -w /workspace dos-agent-dev make smoke
```

Use a persistent devcontainer when invoking separate `dosctl` commands, because
QEMU must remain in the same container:

```bash
./dosctl start
./dosctl exec "VER"                 # output is returned and remains on VGA
./dosctl exec --serial "DIR /S"     # complete capture for long output
./dosctl screen                         # plain 7-bit ASCII (default)
./dosctl screen --text                  # decoded CP437 text
./dosctl screen --json                  # cells and VGA attributes
./dosctl screen --png build/screen.png  # display screenshot
./dosctl type "DIR\r"
./dosctl key PGDN
./dosctl stop
```

Every start creates `build/runs/<run-id>/disk.qcow2`; the canonical
`build/dos71.qcow2` is only a backing image.

## Deploying a program

Copy files into `payload/` using their desired DOS path and rebuild:

```bash
cp MYPROG.EXE payload/BIN/MYPROG.EXE
make runtime
./dosctl start
./dosctl exec "MYPROG.EXE"
```

Stop all active runs before rebuilding the canonical image.

## Tests

```bash
make test       # host-side unit tests
make smoke      # full image/boot/keyboard/screen/serial/screenshot test
```

The supplied image currently reports Windows `4.10.1998`. This is DOS 7.1;
Windows 98 SE system files instead report `4.10.2222`.
