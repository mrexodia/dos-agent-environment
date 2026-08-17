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
- NASM `.COM` payload smoke test;
- a separate, pinned optional image for DJGPP, IA-16 GCC, JWasm, bcc/bin86,
  Free Pascal GO32v2, and UPX.

The pinned mTCP release provides DHCP, ping, HTGET, and binary file collection.
The pinned Links 2.30 binary and local fixtures provide the full-screen browser
vertical slice.

## Setup

Use the [Dev Container CLI](https://github.com/devcontainers/cli) to build and
start exactly the environment declared in `.devcontainer/`, without requiring
VS Code:

```bash
npm install -g @devcontainers/cli   # once, if `devcontainer` is unavailable
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . make runtime
devcontainer exec --workspace-folder . make smoke
```

`devcontainer up` is idempotent and reuses the workspace container. This is
preferable to one-shot `docker run` commands for separate `dosctl` operations,
because QEMU must continue running in the same container. To run the complete
host, smoke, networking, collection, and Links suite:

```bash
devcontainer exec --workspace-folder . make integration
```

To verify that the container definition resolves and builds for each supported
host architecture (with a multi-architecture Docker builder configured):

```bash
devcontainer build --workspace-folder . --platform linux/amd64
devcontainer build --workspace-folder . --platform linux/arm64
```

Inside VS Code's devcontainer (or an already-open container shell), use the
short equivalents:

```bash
make runtime
make smoke
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

Commands that return ordinary results accept `--output-json`; `screen --json`
returns the complete text cell grid and VGA attributes. `collect` uses the
pinned mTCP Netcat while a run is active. For a stopped run, pass its
`--run-id` and the same command safely converts the disposable qcow2 chain and
extracts the partition before using mtools. For example:

```bash
./dosctl start --run-id my-run --output-json
./dosctl exec --run-id my-run --output-json "VER"
./dosctl collect --run-id my-run --output-json C:\\CONFIG.SYS
./dosctl status --run-id my-run --output-json
./dosctl stop --run-id my-run
./dosctl collect --run-id my-run C:\\CONFIG.SYS build/config.sys
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

## Optional DOS cross-toolchains

The large compiler image is deliberately separate from the core harness. The
optional Dev Container pulls a pinned amd64/arm64 image from GHCR, then
compile/runs every hello-world output in DOS with:

```bash
devcontainer up --workspace-folder . \
  --config .devcontainer-toolchains/devcontainer.json
devcontainer exec --workspace-folder . \
  --config .devcontainer-toolchains/devcontainer.json \
  make toolchain-smoke
```

See [`toolchains/README.md`](toolchains/README.md) for pins, outputs, and
multi-architecture build commands.

## Tests

```bash
make unit        # fast host-side unit tests
make smoke       # cold boot, VGA/serial, network, collection, PNG, crash recovery
make integration # smoke plus the complete Links browser vertical slice
make test        # alias for the complete integration suite
```

The supplied image currently reports Windows `4.10.1998`. This is DOS 7.1;
Windows 98 SE system files instead report `4.10.2222`.
