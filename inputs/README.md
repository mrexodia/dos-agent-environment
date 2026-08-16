# Required inputs and provenance

These files are immutable build inputs. The build refuses to start if any file
is missing, has no approved entry in `SHA256SUMS`, or fails its checksum.
Generated images containing Microsoft files must not be distributed.

| Expected filename | SHA-256 | Upstream/source | Licensing and distribution note |
|---|---|---|---|
| `Windows98_SE_No_Ramdrive.img` | `e50bac4bc6cd1861d908fb5aaf147644a82c7f9802b5612a33088e93106780d3` | Human-supplied Windows 98 boot floppy; no public download URL is approved | Microsoft proprietary software. Do not redistribute it or generated disk images. Its current system files report Win98 `4.10.1998` despite the filename. |
| `pcntpk.com` | `a79d1a03a6a9c9873097f38f5708061d1c8a4e17f8c7239e799d020314fc592c` | Human-supplied PCNTPK-DOS 03.10 binary; no upstream URL for this exact binary is approved | Packet-driver skeleton identifies Crynwr Software and says it is free software. Preserve its accompanying license when redistributing separately. |
| `cwsdpmi.exe` | `ab87ddfac8147f119d91605fda32323d3cd4764ec49613c9554b3f5d93d4efa2` | [CWSDPMI upstream](https://sandmann.dotster.com/cwsdpmi/) — human supplied r7 binary | CWSDPMI is freely redistributable under its upstream terms; this binary identifies CWSDPMI r7, copyright C.W. Sandmann. |
| `mTCP_2025-01-10_upx.zip` | `89d9374fe07b091fb0f3e5ea2b10acb159f55859835d287eec0d491cd08d340e` | [Official mTCP site](http://www.brutman.com/mTCP/mTCP.html), release 2025-01-10 | GPL-3.0; archive includes `COPYING.TXT`. |
| `links-2.30.exe` | `b857a4a86dbb4d7c633403f29b3f2bec06b971ab83b516cbbecf2ce93f3b2ddf` | [Official Twibright Links downloads](http://links.twibright.com/download.php), DOS 2.30 binary | GPL-2.0-or-later. |

The two human-supplied files without an approved public URL must not be
silently replaced from a mirror. Ask the user to approve a source and checksum
before updating them. `inputs/` is treated as read-only by all project scripts.
