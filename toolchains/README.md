# Optional DOS cross-toolchains

The optional image is separate from the core harness because DJGPP and IA-16
GCC are large, slow source builds. Nothing in the normal `make runtime` or
`make test` path depends on this image. A prebuilt multi-architecture image is
published as `ghcr.io/mrexodia/dos-agent-environment/toolchains:v2` (also
`latest`); the Dev Container configuration pins its immutable manifest digest
`sha256:d7686f05549beb2773fc2691817b4221f7bc07ad5b104912c628d9c2b17b757d`.
Docker selects the amd64 or arm64 payload automatically.

## Included toolchains

| Tool | Pinned source/version | DOS test output |
|---|---|---|
| DJGPP GCC | `andrewwutw/build-djgpp` commit `0dc2836`, invoked as `./build-djgpp.sh 12.2.0` | 32-bit DPMI EXE using CWSDPMI |
| IA-16 GCC + libi86 | `tkchia/build-ia16` commit `00a8c6a` and the component commits in `Dockerfile.toolchains` | 16-bit real-mode DOS EXE |
| JWasm | commit `a5c4ea0` | tiny-model `.COM` |
| bcc/bin86 | Debian Bookworm packages pinned to `0.16.17-3.4` (`bcc`, `bin86`, and `elks-libc`) | small-model `.COM` |
| Free Pascal | upstream FPC 3.2.2 commit `0d122c4` | i8086 real-mode and i386 GO32v2 DOS EXEs |
| Open Watcom V2 | dated build `2026-08-01-Build`, archive SHA-256 `e1bc4e8…` | 16-bit MZ, 16-bit COM, 16-bit C++, and 32-bit DOS/4GW outputs |
| FASM | 1.73.35 archive SHA-256 `a34dec7…` | flat `.COM` |
| NASM | Debian Bookworm package in the core image | flat `.COM` payloads |
| UPX | upstream v4.2.4 commit `7685c5f` | compressed DJGPP EXE |

The IA-16 image follows the upstream documented Linux stages: `clean`,
`binutils`, `prereqs`, `gcc1`, `newlib`, `causeway`, `elks-libc`, `elf2elks`,
`libi86`, and `gcc2`. CauseWay's host helpers and ELKS' emulator use x86-only
host interfaces; their optional stages are skipped on arm64 because the tested
real-mode DOS compiler does not depend on them. The arm64 image retains the
stage-1 C compiler instead of rebuilding the optional stage-2 C++ libraries.

FPC 3.2.2 cannot build i386 or i8086 compilers on an arm64 host because the
host lacks an 80-bit extended floating-point type. Open Watcom and FASM also
publish x86-64 Linux host programs. The arm64 image runs these pinned,
statically linked host tools under `qemu-user-static`; target assemblers and
linkers remain native where available. This exact arrangement passes the same
compile-and-DOS-execution test on both architectures. All component
repositories, prerequisite archive checksums, and bootstrap images are pinned
explicitly.

## Uniform commands

Thin wrappers keep target selection and memory-model defaults explicit:

| Command | Target |
|---|---|
| `dos-cc16-gcc` | IA-16 GCC real-mode C; `DOS_MEMORY_MODEL` defaults to `medium` |
| `dos-cc16-watcom` | Open Watcom real-mode C/C++; model defaults to `s` |
| `dos-cc32-djgpp` | DJGPP protected-mode C/C++ |
| `dos-cc32-watcom` | Open Watcom protected mode using DOS/4GW |
| `dos-pas16` / `dos-pas32` | FPC i8086 real mode / GO32v2 protected mode |
| `dos-asm-nasm` / `dos-asm-masm` / `dos-asm-fasm` | NASM / JWasm / FASM |

Native compiler commands remain available. Keep `/opt/ia16`, `/opt/djgpp`,
`/opt/fpc`, and `/opt/watcom` separate; do not mix their include or library
trees.

## Pull and test with the Dev Container CLI

The toolchain container has a different config file, so it can coexist with the
small core container. Authenticate to GHCR first if the package is private;
then `devcontainer up` pulls the pinned multi-architecture image:

```bash
devcontainer up \
  --workspace-folder . \
  --config .devcontainer-toolchains/devcontainer.json

devcontainer exec \
  --workspace-folder . \
  --config .devcontainer-toolchains/devcontainer.json \
  make toolchain-smoke
```

`make toolchain-smoke` does not accept a successful `--version` as proof. It
builds thirteen outputs covering every compiler, both Open Watcom targets and
C++, both FPC targets, and an UPX-compressed DJGPP executable. It deploys them
through `payload/`, rebuilds the runtime, cold-boots a disposable overlay, and
verifies every marker through the public `DosVM` API.

The architecture payloads behind `v2` are:

- amd64: `sha256:34f7d11ae2b04cf20cfd2566832faf5283b427bd5d309348a1a92b7d4a76d75e`
- arm64: `sha256:17607dde915a01a1e601bd86f9f4de586334c6c0da3796aaba905def8c71edac`

Maintainers can rebuild the pinned extension stages with the separate
source-build configuration:

```bash
devcontainer build --workspace-folder . \
  --config .devcontainer-toolchains-source/devcontainer.json \
  --platform linux/amd64

devcontainer build --workspace-folder . \
  --config .devcontainer-toolchains-source/devcontainer.json \
  --platform linux/arm64
```

The v2 source target extends the immutable, already-tested v1 image, avoiding a
full GCC rebuild. The original DJGPP, IA-16 GCC, JWasm, and UPX source stages
remain individually buildable. The final target consumes a pinned FPC
bootstrap so it works on arm64; maintainers can reproduce that bootstrap on
amd64 with:

```bash
docker build --target fpc-build \
  -f .devcontainer/Dockerfile.toolchains .
```

The normal `.devcontainer-toolchains` workflow never rebuilds compilers.
