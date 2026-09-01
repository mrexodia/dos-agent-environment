#!/usr/bin/env python3
"""Reproduce the input wedge under `qemu -d int` and analyze the trace.

Boots with interrupt logging enabled, runs the PROMPT sequence that wedges,
and on failure analyzes the last interrupt deliveries (keyboard v=09, serial
v=0c, timer v=08, PCNet IRQ11 v=73) to see which edges stop arriving and what
runs last around the loss.
"""
import os
import re
import secrets
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
LOG = "/tmp/inttrace.log"
os.environ["DOSCTL_QEMU_ARGS"] = f"-d int -D {LOG}"

from harness.dosvm import DosVM  # noqa: E402

VECTORS = {
    "0x08": "timer IRQ0",
    "0x09": "keyboard IRQ1",
    "0x0c": "serial IRQ4",
    "0x73": "PCNet IRQ11",
    "0x2f": "int 2f",
    "0x28": "int 28",
}

for attempt in range(20):
    if os.path.exists(LOG):
        os.unlink(LOG)
    try:
        vm = DosVM.start(run_id=f"int{attempt}", timeout=90.0)
    except Exception as e:
        print(f"attempt {attempt}: start failed ({str(e).splitlines()[-1][:80]}), retrying")
        time.sleep(2.0)
        continue
    try:
        vm.wait_for_prompt(timeout=60.0)
        token = f"DOSCTL{secrets.token_hex(4).upper()}"
        vm.type(f"PROMPT {token}$P$G\r")
        vm.wait_for_prompt(timeout=10.0, token=token)
        print(f"attempt {attempt}: OK")
        continue
    except Exception as e:
        print(f"attempt {attempt}: WEDGE — analyzing {LOG}")
        print("  exception:", [l for l in str(e).splitlines() if "operation" in l])
        vm.run_dir.mkdir(exist_ok=True)
        Path(vm.run_dir / "int.log").symlink_to(LOG)
        if not os.path.exists(LOG) or os.path.getsize(LOG) == 0:
            print("  trace file missing/empty")
            break
        lines = Path(LOG).read_text(errors="replace").splitlines()
        print(f"  trace lines: {len(lines)}")
        with vm.qmp() as q:
            pic = q.hmp("info pic")
            for l in pic.splitlines():
                if l.startswith(("pic0", "pic1")):
                    print("   ", l)
            for i in range(3):
                regs = q.hmp("info registers")
                for l in regs.splitlines():
                    if "EIP=" in l or l.startswith("CS ="):
                        print(f"    s{i}:", l[:100])
                time.sleep(1.0)
            for l in regs.splitlines():
                if "EIP=" in l or l.startswith(("CS =", "SS =", "ES =", "DS =")):
                    print("   ", l[:120])
            q.execute("pmemsave", {"val": 0, "size": 0x110000,
                                   "filename": "/tmp/wedge.bin"})
            print("    pmemsave -> /tmp/wedge.bin")
        events = []
        for idx, line in enumerate(lines):
            m = re.search(r"Servicing hardware INT=0x([0-9a-f]{2})", line)
            if m:
                events.append((idx, m.group(1), line.strip()))
        for want, name in VECTORS.items():
            hits = [ev for ev in events if ev[1] == want]
            if hits:
                idx = hits[-1][0]
                after = sum(1 for ev in events if ev[0] > idx)
                print(f"  last {name} (v={want}) at line {idx}, {after} int events after it")
            else:
                print(f"  {name} (v={want}): never delivered")
        # how many key IRQs were serviced vs typed characters
        kb = [ev for ev in events if ev[1] == "09"]
        print(f"  keyboard IRQ1 serviced: {len(kb)} times; keystrokes typed: ~{len(token) + 12}")
        print("  --- last 25 trace lines ---")
        for l in lines[-25:]:
            print("   ", l.strip()[:150])
        break
    finally:
        vm.stop(force=True)
