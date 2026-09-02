# wolfSSL TLS 1.3 for DJGPP (DOS)

wolfSSL 5.8.0-stable cross-built for DJGPP (GCC 5.2, i586-pc-msdosdjgpp),
linked against Watt-32 for sockets. Verified in the DOS VM: full TLS 1.3
handshake + encrypted HTTP round trip against a Python TLS 1.3-only server
(scripts/run-tlstest.py), marker TLS13-DOS-OK-42.

## wolfSSL build recipe (build/wolfssl, git-ignored)

    ./autogen.sh                       # needs libtool
    CC=i586-pc-msdosdjgpp-gcc \
    CPPFLAGS='-DWOLFSSL_NO_SOCK -DWOLFSSL_USER_IO \
              -DALIGN32= -DALIGN64= -DALIGN128= \
              -DCUSTOM_RAND_GENERATE_SEED=dos_rand_seed' \
    ./configure --host=i586-pc-msdosdjgpp --enable-tls13 \
                --enable-opensslextra --disable-examples --disable-crypttests
    make -j8            # -> src/.libs/libwolfssl.a

- `WOLFSSL_NO_SOCK/USER_IO`: no sys/socket.h in DJGPP; the application
  provides IO callbacks (watt32 send/recv) via wolfSSL_CTX_SetIORecv/IOSend.
- `ALIGN32=` etc.: COFF object alignment limit is 16.
- `CUSTOM_RAND_GENERATE_SEED=dos_rand_seed`: DOS has no /dev/urandom;
  dosrand.c mixes RDTSC + BIOS tick counter + clock() into xorshift32.
  Without it wolfSSL_CTX_new fails silently.
- `--enable-opensslextra`: OpenSSL compatibility layer so Links' https.c
  can be ported with minimal changes (next step).

## Test client (tlstest.c + dosrand.c here)

DJGPP exe: link tlstest.c dosrand.c libwolfssl.a libwatt.a -lm.
Driver: scripts/run-tls13-server.py cert generation + scripts/run-tlstest.py.
