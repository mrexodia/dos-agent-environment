# DOS Agent Environment — Implementation Plan

## Implementation status

Milestones A through D are implemented. `make smoke` verifies cold boot, VGA
and serial execution, CP437/attribute scraping, PNG capture, DHCP, host ping,
HTGET, binary and empty-file collection, post-mortem extraction, immutable base
images, and recovery from a killed QEMU process. `make integration` additionally
verifies the complete Links load/link/form/scroll/screenshot/crash-recovery
vertical slice. Fast protocol and decoding unit tests run under `make unit`; `make test` runs everything.
The Dev Container CLI workflow is documented in `README.md`.

Milestone E is implemented in a separate optional multi-architecture GHCR
image. DJGPP, IA-16 GCC, Open Watcom V2, JWasm, FASM, bcc/bin86, both Free
Pascal DOS targets, and UPX are pinned independently from the core harness;
each output is compile-and-run
tested in DOS on amd64 and arm64. See `toolchains/README.md` for the prebuilt
and source-build Dev Container CLI workflows.

## Goal

Build a small, scriptable DOS 7.1 environment that an agent can use to deploy,
run, inspect, and test DOS programs.

The primary agent workflow is:

```text
host build -> copy outputs into payload/ -> make runtime -> dosctl start
           -> dosctl exec/type/key/screen/screenshot -> dosctl stop
```

The first full-screen application is the text-mode Links browser. The generic
harness must not contain Links-specific behavior.

## Scope and principles

- DOS runs in QEMU using i386 TCG on both arm64 and amd64 hosts. KVM is neither
  required nor assumed.
- Compilation happens on Linux with cross-compilers. Compiling inside DOS is
  out of scope for the first version.
- The DOS disk is rebuilt from declared inputs, `guest/`, and `payload/`; it is
  not maintained by hand.
- Start with a 504 MiB FAT16 disk (1024 cylinders, 16 heads, 63 sectors). It is simpler to construct and more than
  large enough for the initial projects. FAT32 is a later compatibility test,
  not an MVP requirement.
- Cold boot is the default. DOS boots quickly, and avoiding snapshots initially
  removes substantial state and corruption complexity.
- Every VM runs from a disposable qcow2 overlay. Tests never write to the
  canonical base image.
- Text-mode screen scraping is the primary automation surface. PNG screenshots
  provide the agent and humans with a real view of the display.
- Use pattern/event waits, not fixed sleeps.

“Deterministic” means behaviorally repeatable from pinned inputs. Bit-identical
images are not promised unless FAT timestamps and all build metadata are later
normalized.

## Repository layout

```text
.
├── .devcontainer/
│   ├── devcontainer.json
│   └── Dockerfile
├── inputs/                       # necessary files
│   ├── Windows98_SE_No_Ramdrive.img
│   ├── mTCP_2025-01-10_upx.zip
│   ├── pcntpk.com
│   ├── cwsdpmi.exe
│   └── links-2.30.exe
├── guest/                        # text files stored with DOS CRLF endings
│   ├── CONFIG.SYS
│   ├── AUTOEXEC.BAT
│   ├── MSDOS.SYS
│   ├── BIN/SERIAL.BAT
│   └── MTCP/TCP.CFG
├── payload/                      # mirrors the guest filesystem root
│   └── BIN/
├── scripts/
│   ├── build-image.sh
│   ├── build-runtime.sh
│   └── smoke.py
├── harness/
│   ├── dosctl.py                 # stable agent-facing CLI
│   ├── dosvm.py                  # VM lifecycle and high-level operations
│   ├── qmp.py
│   ├── keymap.py
│   ├── screen.py
│   └── collect.py
├── apps/
│   └── links/
│       ├── webroot/
│       ├── driver.py
│       └── test_browse.py
├── tests/
├── build/                        # generated and gitignored
│   ├── dos71.img
│   ├── dos71.qcow2
│   └── runs/<run-id>/
└── Makefile
```

Future applications get one directory under `apps/`, containing their optional
host build script, fixtures, tests, and helpers built on the public harness.

## 1. Core devcontainer

The first container contains only what is required to build the disk and run
the harness:

```dockerfile
FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    qemu-system-x86 qemu-utils \
    mtools parted dosfstools \
    build-essential ca-certificates gettext curl unzip git nasm \
    python3 python3-pexpect python3-pytest python3-pil \
    netcat-openbsd socat \
    && rm -rf /var/lib/apt/lists/*

# Pin the revision; update it only together with the disk-build smoke test.
ARG MS_SYS_REF=318df77ec01fbdc4084263fa9a828f662ce3c685
RUN git clone https://github.com/pbatard/ms-sys /tmp/ms-sys \
    && cd /tmp/ms-sys && git checkout "$MS_SYS_REF" \
    && make && make install \
    && rm -rf /tmp/ms-sys
```

`upx-ucl` is not in Debian Bookworm and must not be in the core `apt-get` line.

`devcontainer.json` forwards port 8080 for test fixtures. VNC is optional and
not part of the agent API; QMP screenshots work with `-display none`.

### Optional toolchain image

Do not block the harness on large or fragile compiler builds. Add a separate
Dockerfile stage only after the core acceptance tests pass:

- DJGPP: 32-bit protected-mode C/C++. The current build-djgpp invocation uses
  `./build-djgpp.sh 12.2.0`, not `gcc1220`.
- gcc-ia16: real-mode C. The maintained build scripts are at
  `https://gitlab.com/tkchia/build-ia16` (also mirrored on Codeberg), not the
  old GitHub URL. Follow and pin the current documented build stages.
- JWasm: MASM-compatible assembly.
- `bcc`/`bin86`: small 16-bit C tools.
- Free Pascal DOS cross-compilers.
- UPX, if useful, from a pinned upstream source or release.

Each optional compiler gets an actual hello-world compile-and-run test. A
successful version command alone is insufficient.

**Core done when:** QEMU, mtools, `ms-sys`, NASM, pytest, Pillow, and the Python
QMP client tests work on both arm64 and amd64 container builds.

## 2. Inputs and provenance

Move the existing boot floppy, packet driver, and CWSDPMI files into `inputs/`.
Fetch mTCP and the prebuilt DOS Links binary only from documented sources. Ask
before using an uncertain mirror.

Maintain a checked-in `inputs/README.md` containing:

- expected filename;
- upstream/source URL;
- expected SHA-256 supplied or approved by the human;
- licensing/distribution note.

The build validates all expected checksums before doing any work and prints a
clear missing-input error.

Initial inspection:

```bash
file inputs/*
unzip -l inputs/mTCP_2025-01-10_upx.zip
mdir -i inputs/Windows98_SE_No_Ramdrive.img ::
```

## 3. Build the bootable DOS disk without root

A whole-disk raw image and a partition image are separate while building. This
avoids loop devices and avoids incorrectly asking `ms-sys` to write a boot
record through an mtools-style `@@offset`.

`ms-sys --partition` does not accept a partition number. Never run
`ms-sys --fat16` or `--fat32` against the whole-disk image; that location is the
MBR, not the partition boot sector.

`scripts/build-image.sh` performs these steps:

1. Create a 504 MiB temporary whole-disk image (exactly 1024×16×63 sectors).
2. Create an MBR partition table with one active FAT16 partition starting at
   sector 2048 (1 MiB).
3. Read the exact start sector and length back from `parted -m ... unit s print`.
4. Create a separate temporary partition image with that exact length.
5. Format the partition image with mtools, explicitly setting the BPB hidden
   sector count to the partition start:

   ```bash
   mformat -i "$part_img" -h 16 -n 63 -H "$start_sector" -v DOS71 ::
   ms-sys --force --fat16 "$part_img"
   # A standalone image is formatted as drive 0x00; FAT16 C: requires 0x80.
   printf '\x80' | dd of="$part_img" bs=1 seek=36 conv=notrunc
   ```

   If QEMU reports different BIOS geometry during implementation, make the
   geometry explicit and keep it consistent between `mformat` and QEMU.
6. Copy `IO.SYS` and `COMMAND.COM` from the floppy to a temporary host
   directory. Copy `IO.SYS` to the empty partition first, followed by our
   `MSDOS.SYS` and then `COMMAND.COM`. This preserves the old DOS requirement
   that the system loader be allocated first.
7. Set system/hidden/read-only attributes on `IO.SYS` and `MSDOS.SYS`.
8. Create `DOS`, `MTCP`, `DRIVERS`, `BIN`, and `TMP`, then copy the declared
   base files.
9. Copy every existing top-level entry from `payload/` recursively. An empty
   `payload/` is valid and must not pass a literal `payload/*` to `mcopy`.
10. Copy the completed partition image into the whole-disk image at the parsed
    start sector using `dd ... conv=notrunc,sparse`.
11. Write the Windows 95B/98 MBR to the whole-disk image:

    ```bash
    ms-sys --force --mbr95b "$disk_img"
    ```

12. Atomically rename the temporary whole-disk image to `build/dos71.img`.

Run `fsck.fat -vn` against the temporary partition image and inspect it with
`mdir` before assembly. The script uses `set -euo pipefail`, temporary files,
and cleanup traps.

If FAT16 construction proves incompatible with the supplied IO.SYS, stop and
record the boot output before changing strategies. The next option is the same
partition-image procedure with FAT32 (`mformat -F` and `ms-sys --fat32`), not a
manual mutable base image. Booting the floppy and running `SYS C:` is a
one-time diagnostic, not an accepted production build path.

### Guest configuration

All guest text files use CRLF endings.

- `MSDOS.SYS`: `BootGUI=0`, `Logo=0`, and `AutoScan=0`, with the appropriate
  root paths.
- `CONFIG.SYS`: load `HIMEM.SYS`; set `DOS=HIGH`, `FILES=40`, and
  `LASTDRIVE=Z`.
- `AUTOEXEC.BAT`: set `PATH`, `MTCPCFG`, and a predictable prompt; load
  `PCNTPK` on interrupt `0x60`; run mTCP DHCP. Do not run `CTTY COM1` here.
- `TCP.CFG`: contain `PACKETINT 0x60` and `HOSTNAME DOSBOX`; DHCP writes its
  lease only into the disposable run overlay.
- `SERIAL.BAT`: contain `CTTY COM1`. The harness must also test restoration
  with `CTTY CON`.

**Done when:** structural checks pass and a QEMU cold boot reaches a visible
`C:\>` prompt without using the floppy.

## 4. Runtime image and safe deployment

The deployment mechanism is deliberately simple:

```text
cross-compile -> copy into payload/... -> make runtime -> cold boot -> test
```

Make targets:

- `make image`: rebuild `build/dos71.img`.
- `make runtime`: run `make image`, then atomically create
  `build/dos71.qcow2` from the raw image.
- `make smoke`: create a disposable run and execute smoke tests.
- `make test`: run all tests.

`build/dos71.qcow2` is a read-only base by convention. `dosctl start` creates:

```bash
qemu-img create -f qcow2 -F qcow2 \
  -b "$(realpath build/dos71.qcow2)" \
  "build/runs/$run_id/disk.qcow2"
```

QEMU writes only to that overlay. `make runtime` refuses to replace its base
while an active run references it. `dosctl stop --all` is the normal action
before rebuilding.

Never run mtools against an image currently opened writable by QEMU. Never run
QEMU directly against the raw source image.

Snapshots are deferred until measurements show cold boot is a meaningful
bottleneck. If added, the prepared image is still cloned per run and tests
must prove that networking survives repeated restores.

## 5. VM lifecycle

`DosVM` owns the QEMU process and all files under `build/runs/<run-id>/`:

```text
qmp.sock
serial.sock
qemu.pid
qemu.log
disk.qcow2
screen.ppm / screen.png
```

Representative invocation:

```bash
qemu-system-i386 \
  -accel tcg \
  -m 64 \
  -boot order=c \
  -drive file="$run_disk",format=qcow2,if=none,id=dosdisk \
  -device ide-hd,drive=dosdisk,bus=ide.0,unit=0,cyls=1024,heads=16,secs=63 \
  -nic user,model=pcnet \
  -qmp unix:"$run_dir/qmp.sock",server=on,wait=off \
  -serial unix:"$run_dir/serial.sock",server=on,wait=off \
  -display none \
  -no-reboot
```

Do not use fixed `/tmp` socket names or fixed serial ports. `dosctl` waits for
the QMP greeting and capabilities handshake before reporting that a run has
started. Stop first attempts a QMP quit, then terminates and finally kills with
bounded timeouts. Stale PID/socket detection must not kill an unrelated
process.

Slirp defaults are guest `10.0.2.15`, host `10.0.2.2`, and DNS `10.0.2.3`.
Fixture servers inside the container bind a reachable address, not only an
unrelated host namespace.

## 6. Agent-facing `dosctl` interface

The CLI is the stable public API. Tests and app drivers call the same Python
methods underneath it.

Minimum commands:

```text
dosctl start [--run-id ID]
dosctl status [--run-id ID]
dosctl exec [--timeout SEC] [--serial] "DOS command"
dosctl type "text"
dosctl key ENTER|ESC|UP|DOWN|PGUP|PGDN|...
dosctl wait [--timeout SEC] "regular expression"
dosctl screen [--text|--json|--png [output.png]]
dosctl screenshot [output.png]  # backwards-compatible alias
dosctl collect DOS_PATH [HOST_PATH]
dosctl stop [--run-id ID|--all]
```

Successful commands emit concise text by default and support structured JSON
for agents. Every timeout includes the run ID, operation, QEMU status, and last
text screen; screenshot paths are included when useful.

### Command execution

`exec` is for non-interactive DOS shell commands. By default it executes on
VGA so the command, output, and final prompt remain visible while output is
also returned to the caller. If output scrolls beyond the text screen, it
fails with guidance to use `--serial` for complete capture. Completion requires
the newly active bottom prompt, not an old prompt elsewhere on screen.

The serial backend uses the console safely:

1. At a known VGA shell prompt, set a unique temporary prompt token.
2. Run `CTTY COM1` through keyboard injection.
3. Send the command over the per-run serial socket and capture until the next
   unique prompt.
4. Send `CTTY CON` over serial.
5. Verify that the prompt returned to VGA and restore the normal prompt.

On any serial-backend failure, stop the disposable VM rather than leaving
console ownership ambiguous. Interactive and full-screen programs are launched
with `type` and `key`, not `exec`. A regex already present in an unchanged
screen is not considered command completion.

### Keyboard input

Use the native QMP `send-key` command. `keymap.py` covers the full DOS-safe US
ASCII set and named navigation/function keys, including shift chords. Type at a
bounded rate suitable for the DOS keyboard buffer. Start around 40 ms/key and
make it configurable.

## 7. Seeing the screen

Provide two complementary views.

### Text view

For color text mode, read video RAM through QMP/HMP and decode CP437 characters
plus attributes. Do not blindly assume page zero and 80x25; inspect BIOS data:

- current mode at physical `0x449`;
- columns at `0x44A`;
- active-page/start offset at `0x44E`;
- rows minus one at `0x484` where supported.

The MVP supports color mode `0x03` and reports a clear unsupported-mode error
for text scraping otherwise. Preserve a cell grid (`character`, foreground,
background, blink/intensity) internally; `screen` emits plain 7-bit ASCII by
default for text-only agents, `--text` emits decoded CP437, and `--json`
exposes dimensions and cells/attributes needed by app drivers.

`wait()` polls screen generations and requires the requested pattern to be
observed according to the caller's freshness requirement. Timeouts always
include the last screen.

### Image view

Use QMP `screendump` to create a PPM and Pillow to convert it to PNG. This works
with `-display none` and gives the agent a genuine screenshot. It also permits
manual diagnosis of graphics modes even though the first automated app tests
remain text-mode.

**Done when:** after a cold boot, an agent can start a VM, obtain both text and
PNG views, run `VER`, type `DIR`, and stop the VM using only `dosctl`.

## 8. Getting files and results out

Use three paths, in order of preference:

1. `dosctl exec` captures ordinary command output over serial.
2. `dosctl collect` starts a host listener and invokes mTCP Netcat in the guest
   to send a requested file to `10.0.2.2`. Verify the exact NC syntax against
   the pinned mTCP release and test binary data, empty files, timeout, and
   listener cleanup.
3. Post-mortem extraction is allowed only after QEMU has stopped. Convert the
   disposable qcow2 chain to a temporary raw image, extract its partition, and
   use mtools. Never point mtools directly at qcow2.

Screen scraping remains appropriate for full-screen application behavior.

## 9. Acceptance tests

### Core smoke test

From a cold disposable overlay:

1. Wait for a newly visible `C:\>` prompt.
2. `dosctl exec "VER"` contains `4.10.1998` for the currently supplied Win98 image (or `4.10.2222` for Win98 SE); both are DOS 7.1 and do not print “7.1”.
3. Create a file on `C:`, type it back, and collect it to the host.
4. Obtain a valid non-empty PNG screenshot.
5. Run a NASM `.COM` deployed through `payload/` and verify its output.
6. Kill QEMU mid-run, discard the overlay, start again, and pass all checks.

### Protected-mode test

The optional toolchain smoke test cross-compiles DJGPP and Free Pascal GO32v2
hello-world programs, deploys them through `payload/BIN`, and executes them.
This proves CWSDPMI, HIMEM, and the deployment path.

### Network test

1. Verify mTCP DHCP completed.
2. Ping `10.0.2.2`.
3. Serve a marker page from the container on port 8080.
4. Fetch it with mTCP HTGET and verify the marker.
5. Repeat from several fresh overlays.

External ICMP is not required; QEMU slirp may block it.

## 10. Links vertical slice

Start with the known prebuilt binary:

```text
inputs/links-2.30.exe -> payload/BIN/LINKS.EXE -> make runtime
```

Run Links without `-g` against `http://10.0.2.2:8080/index.html`. Use
`dosctl type/key/screen/screenshot` to verify the marker text renders.

`apps/links/driver.py` builds only on the generic `DosVM` API and implements:

- `goto(url)`;
- navigation keys and Enter;
- `read_full_page()` by scraping and paging until stable;
- form entry;
- `quit()` with confirmation handling.

Do not hard-code selected-link attribute constants without recording them from
the pinned Links version. Prefer behavior-oriented assertions where possible.

Tests cover:

1. load a fixture page;
2. follow a link;
3. fill and submit a form;
4. scroll a long page;
5. capture text and PNG while Links is active;
6. kill the VM and rerun from a fresh overlay.

Building Links from source is a later project. Treat WATT-32, zlib, and Links
as independently tested cross-build steps. Do not let source-build work block
the prebuilt end-to-end proof. If HTTPS is later needed, prefer a controlled
TLS-terminating proxy on `10.0.2.2` before attempting to maintain a DOS OpenSSL
port.

## 11. Milestones

### Milestone A — boot

- Core container builds on arm64 and amd64.
- Rootless disk build passes structural checks.
- DOS reaches `C:\>` from the hard disk.

### Milestone B — agent tool

- `dosctl` owns VM lifecycle.
- Agent can execute shell commands, send keys, scrape text, and obtain PNGs.
- Crash cleanup and disposable overlays work.

### Milestone C — deployment and networking

- Payload `.COM` test passes.
- Serial output and file collection pass.
- DHCP, ping-to-host, and HTGET tests pass.

### Milestone D — Links

- Prebuilt Links loads and navigates all local fixtures in text mode.
- Runs are repeatable after forced QEMU termination.

### Milestone E — optional toolchains

- [x] Add DJGPP and prove a protected-mode binary in the guest.
- [x] Add IA-16 GCC, Open Watcom V2, JWasm, FASM, bcc/bin86, both Free
  Pascal DOS targets, and UPX.
- [x] Compile and execute a real DOS hello-world output from every tool.

## Troubleshooting guide

| Symptom | Check |
|---|---|
| `Invalid system disk` | Partition boot code was written to the partition image; hidden sectors equal 2048; IO.SYS was copied first; active partition and MBR are present. |
| MBR/partition disappeared | `ms-sys --fat16` was incorrectly run against the whole disk. Rebuild; never repair generated images by hand. |
| Blank boot hang | Capture PNG and QEMU log; verify MBR type and BIOS geometry. |
| PCNTPK reports no adapter | QEMU NIC model must be `pcnet`. |
| DHCP timeout | Packet driver load, interrupt `0x60`, and matching `PACKETINT` setting. |
| HTGET works but internet ping fails | Expected slirp ICMP limitation. |
| DJGPP reports no DPMI | CWSDPMI is on PATH and HIMEM loaded. |
| Text scrape is unsupported/garbled | Inspect screenshot and video mode; app may have entered graphics or another text layout. |
| Dropped keystrokes | Increase the configurable per-key delay. |
| `exec` loses its prompt | CTTY restoration failed; discard the run and start a fresh overlay. |
| Stale socket/PID | Use per-run state and verify the recorded PID belongs to this QEMU before cleanup. |
| Corrupted or inconsistent run | Discard its overlay. Do not modify or debug the canonical base. |

## Non-negotiable rules

1. `inputs/` is read-only and generated Microsoft-containing images are never
   distributed.
2. Deployment goes through `guest/`, `payload/`, and a rebuild—not hand edits.
3. The canonical qcow2 is never used as a writable test disk.
4. No mtools access to a live writable VM disk.
5. No fixed sleeps for readiness or command completion.
6. Every timeout reports the last screen and run diagnostics.
7. Wedged or ambiguous VM state is discarded, not repaired in place.
8. App-specific behavior stays under `apps/<name>/`.
9. Text mode is the first automation target; PNG screenshots remain available
   for every display mode QEMU can capture.
10. Optional compilers cannot block delivery of the core agent tool.
