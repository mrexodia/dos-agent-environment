# Required inputs

These third-party files are supplied by the user, are read-only, and are
intentionally not tracked. Links and mTCP are free/open-source software; the
Windows 98 boot image is proprietary. Generated images containing Microsoft
files must not be distributed.

| File | Purpose | Source/licensing note |
|---|---|---|
| `Windows98_SE_No_Ramdrive.img` | DOS 7.1 system and utility files | User-supplied image; its current system files report Win98 `4.10.1998` despite the filename; Microsoft proprietary software |
| `pcntpk.com` | AMD PCnet packet driver | User-supplied Crynwr-style packet driver |
| `cwsdpmi.exe` | DPMI host for DJGPP programs | User-supplied CWSDPMI binary |
| `mTCP_2025-01-10_upx.zip` | DHCP, ping, HTGET, and NC | mTCP 2025-01-10; GPL-3.0; official distribution |
| `links-2.30.exe` | Initial Links vertical slice | Links 2.30; GPL-2.0-or-later; official Twibright DOS binary |

`SHA256SUMS` records approved hashes for files currently present. Add hashes
only after the user approves newly downloaded inputs.
