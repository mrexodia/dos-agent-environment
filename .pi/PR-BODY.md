# Reliability: DHCP hostname, input-wedge root cause, KVM acceleration, idle HLT TSR

## Summary

Four reliability/quality improvements for the harness, developed while chasing an intermittent guest hang that made `make smoke` flaky:

- **Fix the boot-time DHCP hostname warning.** mTCP's DHCP client requests `HOSTNAME DOSBOX` from `C:\MTCP\TCP.CFG`, but slirp only returns DHCP option 12 when `hostname=` is passed on the NIC. The default `-nic` now includes `hostname=DOSBOX` (overridable via `DOSCTL_QEMU_NIC`), and `DOSCTL_QEMU_ARGS` allows extra QEMU arguments for experiments.

- **Root-cause the intermittent input wedge.** Roughly 30–50% of TCG boots, the guest became permanently unresponsive to keyboard and serial input right after DHCP or mid-PING. `scripts/repro-ping-hang.py` and `scripts/probe-int-trace.py` (`qemu -d int`) show: every keystroke IRQ *is* delivered by QEMU; after the last one, **no interrupt is serviced at all** while the CPU keeps running with IF=1; live PIC state shows `isr=02` — timer IRQ0 In-Service **without EOI**, which blocks all lower-priority master IRQs forever. Disassembly of wedge dumps pinpoints Win98 SE IO.SYS's real-mode interrupt reflector (segment 0x0696): its reentrancy-busy path executes `cli; out 21h,0FFh; out A1h,0FFh` and waits on a condition that never comes, orphaning the in-flight timer EOI. Full write-up in `PING-HANG-NOTES.md`. Also included: an ISA NE2000 packet-driver option (`payload/DRIVERS/NE2000.COM`, preferred by AUTOEXEC when present) and probe scripts used to rule out NIC/mTCP/slirp as the trigger.

- **Use KVM when available.** With nested virtualization, `/dev/kvm` is passed into the Dev Container (`runArgs --device=/dev/kvm`) and the harness selects `-machine accel=kvm:tcg` when the device is writable (overridable via `DOSCTL_QEMU_ACCEL`), keeping TCG as automatic fallback elsewhere. Results: boot-to-prompt ~2 s (was ~30 s), **26/26 clean PROMPT/CTTY/PING cycles with zero wedges**, and the full smoke suite passes including crash recovery. KVM's interrupt timing avoids the reflector race; TCG remains available for deterministic reproduction.

- **Add `IDLE.COM`, an 87-byte INT 28h HLT TSR.** Win98 real-mode DOS busy-polls in its INT 28h idle path, so even under KVM a running VM burns a full core at the prompt. The TSR hooks INT 28h, executes `STI; HLT`, and chains; AUTOEXEC loads it after PATH. Measured QEMU CPU at an idle DOS prompt: **100.7% → 1.0%**. PING/DIR/Links unaffected; smoke suite passes.

## Testing

- `make smoke` passes end-to-end (twice, including crash recovery and post-mortem extraction) under KVM with the TSR loaded.
- Manual: `PING -a 10.0.2.2` 4/4 replies; Links 2.30 browses HTTP (win3x.org) and TLS 1.2 HTTPS (netlify.app) through slirp.
- Wedge reproduction scripts kept under `scripts/` for future TCG debugging.
