# Building the DOS version of Links 2.30 from source

This documents how `BIN/LINKSDEV.EXE` was cross-compiled from the official
[links-2.30.tar.gz](https://links.twibright.com/download/links-2.30.tar.gz)
sources (GPL-2.0) entirely inside this workspace, using a DJGPP cross
toolchain and the Watt-32 TCP/IP stack — and how the build used the DOS
agent environment itself to generate target-specific data.

## Toolchain

The optional toolchains Dev Container image (`ghcr.io/.../toolchains`) is not
publicly pullable yet, so a DJGPP cross compiler (GCC 5.2.0) was extracted
from the Docker Hub image `gstolarz/djgpp` into `tools/djgpp` (git-ignored,
130 MB). In the core Dev Container:

```bash
export PATH=$PWD/tools/djgpp/bin:$PATH
```

## Watt-32 (BSD sockets over the packet driver)

Links needs a BSD socket API; on DJGPP that is
[Watt-32](https://github.com/gvanem/Watt-32), which talks to the same
PKTDRVR interface (PCNTPK) the harness already loads.

1. `tar xzf watt32.tar.gz` (master snapshot) into `build/watt32`
2. `gcc -O2 -o util/linux/bin2c util/bin2c.c` (host helper)
3. `cd src && ./configur.sh djgpp` generates `djgpp.mak`
   (with `DJGPP_PREFIX` auto-detected from `$PATH`)

The generated errno data files do not exist in the git snapshot because they
must be produced by a program *running under DJGPP itself*. The DOS agent
environment solves this bootstrapping problem directly:

4. `i586-pc-msdosdjgpp-gcc -O2 -I inc -I src -o payload/BIN/DJERR.EXE util/errnos.c`
5. `make runtime`, boot the VM, then in the guest:

   ```
   C:\BIN\DJERR.EXE -s > C:\TMP\SYSERR.C
   C:\BIN\DJERR.EXE -e > C:\TMP\DJGPP.ERR
   ```

   and collect both files to the host with the harness `collect_file` (mTCP
   Netcat) path. Copy them to `src/build/djgpp/syserr.c` and
   `inc/sys/djgpp.err`.
6. `WATT_ROOT=$PWD/.. make -f djgpp.mak -j8` → `lib/libwatt.a`

## Links

```bash
cd build/links-2.30
CC=i586-pc-msdosdjgpp-gcc \
CFLAGS="-O2 -I<abs>/watt32/inc" \
LDFLAGS="-L<abs>/watt32/lib" \
LIBS="-lwatt" \
./configure --host=i586-pc-msdosdjgpp --disable-graphics
make -j8            # -> links (coff-go32-exe)
```

Notes:
- Links' ancient autoconf requires `CC` as an environment variable, a
  `--build`/`--host` split, and no stale `config.cache`.
- No OpenSSL: this build is HTTP-only (TLS would need a DJGPP OpenSSL port).
- Deploy as `payload/BIN/LINKSDEV.EXE` plus a root `payload/WATTCP.CFG`
  containing `MY_IP = dhcp` (Watt-32 does its own DHCP; it does not read
  mTCP's TCP.CFG). `BIN/CWSDPMI.EXE` is already part of the base image.

## Verified

Booted the DOS disk, ran `C:\BIN\LINKSDEV.EXE http://www.win3x.org/win3board/`:
Watt-32 acquired a lease via DHCP through the packet driver, and the page
rendered (screen capture in the run log; same rendering as the official
`LINKS.EXE` binary).
