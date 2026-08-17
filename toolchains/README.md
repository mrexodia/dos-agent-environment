# Optional DOS cross-toolchains

The optional image is separate from the core harness because DJGPP and IA-16
GCC are large, slow source builds. Nothing in the normal `make runtime` or
`make test` path depends on this image. A prebuilt multi-architecture image is
published as `ghcr.io/mrexodia/dos-agent-environment-toolchains:v1`; the Dev
Container configuration pins its immutable manifest digest
`sha256:98fda1e54976ba3cdeaed3977932dfa7bdfeb956a94b3404a9c44c631bfddca8`.
Docker selects the amd64 or arm64 payload automatically.

## Included toolchains

| Tool | Pinned source/version | DOS test output |
|---|---|---|
| DJGPP GCC | `andrewwutw/build-djgpp` commit `0dc2836`, invoked as `./build-djgpp.sh 12.2.0` | 32-bit DPMI EXE using CWSDPMI |
| IA-16 GCC + libi86 | `tkchia/build-ia16` commit `00a8c6a` and the component commits in `Dockerfile.toolchains` | 16-bit real-mode DOS EXE |
| JWasm | commit `a5c4ea0` | tiny-model `.COM` |
| bcc/bin86 | Debian Bookworm packages pinned to `0.16.17-3.4` (`bcc`, `bin86`, and `elks-libc`) | small-model `.COM` |
| Free Pascal | upstream FPC 3.2.2 commit `0d122c4` | i386 GO32v2 DOS EXE |
| UPX | upstream v4.2.4 commit `7685c5f` | compressed DJGPP EXE |

The IA-16 image follows the upstream documented Linux stages: `clean`,
`binutils`, `prereqs`, `gcc1`, `newlib`, `causeway`, `elks-libc`, `elf2elks`,
`libi86`, and `gcc2`. CauseWay's host helpers and ELKS' emulator use x86-only
host interfaces; their optional stages are skipped on arm64 because the tested
real-mode DOS compiler does not depend on them. The arm64 image retains the
stage-1 C compiler instead of rebuilding the optional stage-2 C++ libraries.

FPC 3.2.2 cannot build an i386 compiler on an arm64 host because the host lacks
an 80-bit extended floating-point type. The arm64 image therefore runs the
pinned, statically linked amd64 `ppcross386` under `qemu-user-static`; its DJGPP
assembler and linker are still arm64-native. This exact arrangement passes the
same compile-and-DOS-execution test on both architectures. All component
repositories, prerequisite archive checksums, and the FPC bootstrap image are
pinned explicitly.

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
cross-compiles all five hello-world sources under `toolchains/hello/`, creates
an additional UPX-compressed DJGPP executable, deploys them through `payload/`,
rebuilds the runtime, cold-boots a disposable overlay, and verifies each marker
through the public `DosVM` API.

The architecture payloads behind `v1` are:

- amd64: `sha256:85d0edf8a44d3a45e96f2860c81666a46e30b85c2105c4e956c4efff971fc699`
- arm64: `sha256:e29c7b8bf937f98e3583b37a608f428095f2e4179f0d65e6e3f54e63ac61d3e3`

Maintainers can reproduce the compiler stages with the separate source-build
configuration:

```bash
devcontainer build --workspace-folder . \
  --config .devcontainer-toolchains-source/devcontainer.json \
  --platform linux/amd64

devcontainer build --workspace-folder . \
  --config .devcontainer-toolchains-source/devcontainer.json \
  --platform linux/arm64
```

Expect a long first source build. Docker caches each independent compiler
stage, so subsequent builds normally reuse DJGPP, IA-16 GCC, JWasm, and UPX
layers. The final source target consumes the pinned FPC bootstrap so the same
Dockerfile works on arm64; maintainers can reproduce that bootstrap on amd64
with `docker build --target fpc-build -f .devcontainer/Dockerfile.toolchains .`.
The normal `.devcontainer-toolchains` workflow never rebuilds compilers.
