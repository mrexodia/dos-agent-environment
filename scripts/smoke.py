#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import signal
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from harness.dosvm import DosVM


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_once() -> None:
    base = Path(__file__).resolve().parents[1] / "build/dos71.qcow2"
    base_hash = _sha256(base)
    vm = DosVM.start(timeout=30.0)
    try:
        version = vm.exec("VER")
        assert any(build in version for build in ("4.10.1998", "4.10.2222")), version

        vm.exec("ECHO DOS_AGENT_FILE> C:\\TMP\\SMOKE.TXT")
        contents = vm.exec("TYPE C:\\TMP\\SMOKE.TXT")
        assert "DOS_AGENT_FILE" in contents, contents

        hello = vm.exec("HELLO.COM")
        assert "DOS_AGENT_HELLO" in hello, hello

        image = vm.screenshot(vm.run_dir / "smoke.png")
        assert image.stat().st_size > 0
        print(f"functional smoke passed in {vm.run_id}; screenshot: {image}")

        # Simulate an agent/tool crash. Only this run's overlay may be damaged.
        pid = vm.pid()
        assert pid is not None
        os.killpg(pid, signal.SIGKILL)
        deadline = time.monotonic() + 5.0
        while vm.is_alive() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert not vm.is_alive(), "QEMU did not terminate after SIGKILL"
        vm.stop(force=True)
    finally:
        if vm.is_alive():
            vm.stop(force=True)

    assert _sha256(base) == base_hash, "canonical qcow2 changed during a run"
    recovered = DosVM.start(timeout=30.0)
    try:
        assert "DOS_AGENT_HELLO" in recovered.exec("HELLO.COM")
        print(f"crash recovery passed in {recovered.run_id}")
    finally:
        recovered.stop(force=True)


if __name__ == "__main__":
    run_once()
