# CTTY COM1 / PING input wedge — investigation notes

Reproducer: `sudo python3 scripts/repro-ping-hang.py` (must run as root because
`build/` is root-owned when built via the Dev Container CLI; also note the
AF_UNIX 108-byte path limit — run the repo from a short path or set
`DOSCTL_RUN_ID` to a short value if `serial.sock` paths get too long).

## Symptom

Roughly 30–50% of cold boots the guest becomes permanently unresponsive to
input, either:

1. mid-`PING` after `CTTY COM1` (serial console), or
2. right after DHCP completes, while typing on the VGA keyboard (echo stops
   mid-command, e.g. `C:\>PROMPT DOSCT`).

Both variants correlate with packet-driver / network activity (DHCP just
finished, mTCP PING running). The VM keeps running (TCG 100% CPU, timer keeps
ticking); only input is dead. No recovery: CR and Ctrl-C over serial are
ignored, further keystrokes are ignored.

## Live-guest evidence (QMP `info irq` / `info pic` / `info registers` + pmemsave)

### Serial (PING) variant

- CPU: real mode, CPL=0, IF=1, EIP=0x201E with CS base 0x0DD80 (DOS kernel,
  IO.SYS). Disassembly at 0xFD97–0xFDAC shows the classic DOS CON input wait:
  poll ring head `[0x22BC]` vs tail `[0x22BD]`, then `int 28h` (idle) and
  `int 2Fh/AX=1680` (release timeslice), loop. Head == tail: the CON RX ring
  never fills.
- `int 28h` vector is a bare `iret` (0x00C9:106C → 0x1CFC) — nothing polls the
  UART from the idle hook.
- IRQ accounting: IRQ0 keeps counting (interrupts are delivered). **IRQ4
  (COM1) has been delivered exactly once for the entire VM lifetime**, even
  though several serial characters (the `PING` command line itself) were
  received and echoed correctly earlier — i.e. the Win98 real-mode serial
  driver normally fills the ring via *polling*, not IRQs.
- PIC: `pic0 imr=0xb8` (IRQ3/4/5 masked — normal DOS idle state when no IRQ
  driven serial) with **IRR bit 4 pending** (an UART INTR edge latched but
  never deliverable/cleared).

### Keyboard (PROMPT) variant

- Same `imr=0xb8`, same pending IRR bit 4 (unexpected: nothing uses COM1 in
  this variant).
- IRQ1 count frozen at the moment the echo truncated — the keystroke edge is
  *lost*: not in IRR, never delivered.
- EIP in SeaBIOS at F000:8A2C, immediately after a `sti; nop; pause; cli; cld;
  retd` idle sequence inside keyboard-related BIOS code (scancode translation
  tables nearby at F000:DA A0). The BIOS waits for an interrupt that will
  never arrive.

## Confirmed correlation

Booting with `DOSCTL_QEMU_ARGS="-nic none"` (no NIC, no packet driver): the
input wedge never occurs — the serial console stays responsive (CR and
Ctrl-C still echo) even when the PING stage "fails" for lack of a packet
driver. With the PCNet NIC active, wedged guests never echo anything. So the
packet driver / PCNet IRQ path is the trigger, not mTCP itself and not the
CTTY mechanism.

## Working hypothesis

The wedge is interrupt-edge loss in the guest that happens when packet-driver
(PCNTPK on PCNet, IRQ11 via the cascade) activity coincides with UART/keyboard
interrupt timing — most likely a Win98 real-mode driver race (serial driver
polling loop starvation or a lost edge while an IRQ line is momentarily
masked), aggravated under QEMU timing. It is not a harness protocol bug: the
QEMU-side serial socket and QMP stay healthy, and `dosctl`'s `exec --serial`
path (which also uses `CTTY COM1`) inherits the same flakiness.

## NE2000 experiment — wedge reduced, not eliminated (correction)

Swapped the PCNet NIC for an ISA NE2000 (`-nic user,model=ne2k_isa`, io=0x300
irq=9) with the Crynwr `NE2000.COM` packet driver from the
[fragglet/crynwr_mirror](https://github.com/fragglet/crynwr_mirror) GitHub
mirror (8693 bytes, SHA-256 `69573724d30a9498f2a81d3b0ca9b79b4a326522d8aa43d111f7a9ddf047bc12`).
An initial 8-boot run (`scripts/repro-ne2k.py`) showed 8/8 boots with a live
console, but extended testing corrected this:

- Repeated runs of the same reproducer later wedged 1-in-3 at the usual spots.
- `scripts/probe-ctty-cycle.py` (repeated CTTY COM1↔CON toggles in one boot)
- wedges 4/4 boots, sometimes already at the `PROMPT` echo (keyboard path).
- Booting **without the DHCP step** (packet driver loaded, no mTCP program
   ever run, no network traffic) still wedges → mTCP and slirp traffic are
  exonerated; a resident packet driver plus CTTY COM1 is sufficient.
- NE2000 + TCG is also noticeably slower (mTCP output trickles), which
  initially masked wedges as slow PING output.

Conclusion: the wedge is not specific to the PCI PCNet IRQ path. It is a
guest-side race that any resident packet driver can trigger; the driver
switch alone is not a fix. The harness default stays PCNet; `DOSCTL_QEMU_NIC`
remains available for experiments. `payload/DRIVERS/NE2000.COM` and the
AUTOEXEC preference logic stay for future debugging.

## Root cause identified — `-d int` trace (2026-09-01)

`scripts/probe-int-trace.py` boots with `-d int -D <log>`, repeats the wedging
PROMPT sequence, and on failure analyzes the trace plus live QMP state.
Findings from a wedged boot:

1. **All keystrokes are delivered**: the trace shows 63–86 `Servicing hardware
   INT=0x09` events (make+break) for ~26 typed characters. Nothing is lost on
   the QEMU side.
2. **After the last keystroke IRQ, no interrupt is serviced at all** — not
   even the timer — for the whole 10 s timeout, while the CPU keeps executing
   (EIP parked in SeaBIOS's `sti; nop; pause` keyboard-wait idle at
   F000:877B, IF=1, SS/DS still the DOS kernel segment 0x00C9).
3. Live PIC state: **`pic0 isr=02` — timer IRQ0 is In-Service and never
   received an EOI**, `irr=10` (a pending, undeliverable serial IRQ4 edge),
   `imr=b8`. With IRQ0 in-service at top priority, the 8259 will not deliver
   any lower-priority master interrupt (IRQ1 keyboard, IRQ4 serial) — input
   is dead while the machine keeps running.
4. Disassembly of the wedge memory dump pinpoints the code: the INT 08/09/70/
   0C vectors all point into a Win98 SE IO.SYS real-mode **interrupt reflector
   at segment 0x0696** with a reentrancy guard flag. Its *busy* path is

   ```asm
   06B36: cli
   06B37: mov al,0xFF
   06B39: out 0x21,al    ; mask ALL master PIC IRQs
   06B3B: out 0xA1,al    ; mask ALL slave PIC IRQs
   06B3D: ...            ; then waits on a condition that never comes
   ```

**Mechanism**: when a hardware interrupt (timer/keyboard) re-enters the
reflector while the guard flag is busy, it takes the path that masks every
IRQ and CLI-spins, and the in-flight timer interrupt is never EOId. Under
QEMU TCG timing this happens intermittently whenever interrupt traffic
collides (typing over a ticking timer; packet-driver ISRs widen the window,
which is why removing the NIC made it vanish). The bug is in Win98 SE's
real-mode interrupt reflector, not in QEMU, mTCP, or the harness.

### Consequences / options

- A different DOS kernel (MS-DOS 6.x, FreeDOS, or Win98 DOS with the
  reflector never armed) should eliminate the wedge entirely.
- Harness-level mitigation: detect the wedge (typed input yields no echo or
  prompt change within N seconds) and cold-boot restart from the immutable
  base image.

## Next steps (ideas)

- Run QEMU with `-d int -D file` on a hanging boot to capture the exact last
  IRQ1/IRQ4 window and who masks/unmasks the PIC (grep for `out 0x21`).
- Test whether the wedge disappears without the packet driver loaded
  (no DHCP in AUTOEXEC) to confirm the correlation.
- Test `pcntpk` with a different IRQ / `-nic user,model=ne2k_isa` (ISA NE2000
  on a classic ISA IRQ) to see whether the PCI level-triggered PCNet IRQ is
  the trigger.
- Mitigation in the harness if root cause stays elusive: detect the input
  wedge (typed input produces no echo/prompt change within N seconds) and
  restart the VM from the immutable base image — cold boot is cheap.

## Artifacts

Probes are captured by `scripts/repro-ping-hang.py` into the run directory
(`build/runs/<id>/hang.bin`, 1 MiB `pmemsave` dump of the wedged guest).
