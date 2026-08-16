"""Transfer a guest file to the host with mTCP Netcat."""

from __future__ import annotations

import re
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from .dosvm import DosVM, DosVMError


def _safe_path(dos_path: str) -> None:
    # Collection uses COMMAND.COM input redirection. Restrict the unquoted
    # operand to DOS-safe path characters, with no wildcards or metacharacters.
    if not dos_path or not re.fullmatch(r"(?:[A-Za-z]:)?[A-Za-z0-9_.$~\\/:-]+", dos_path):
        raise DosVMError(f"unsafe DOS path for collection: {dos_path!r}")


def extract_file(
    vm: DosVM,
    dos_path: str,
    destination: str | Path,
    timeout: float = 30.0,
) -> Path:
    """Post-mortem extraction from a stopped disposable qcow2 chain."""
    _safe_path(dos_path)
    if vm.is_alive():
        raise DosVMError("post-mortem extraction is forbidden while QEMU is running")
    if not vm.disk_path.is_file():
        raise DosVMError(f"run disk is missing: {vm.disk_path}")
    destination = Path(destination).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    temporary.unlink(missing_ok=True)
    deadline = time.monotonic() + timeout

    def run(command: list[str], capture: bool = False) -> subprocess.CompletedProcess[str]:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(command, timeout)
        return subprocess.run(
            command,
            check=True,
            text=True,
            capture_output=capture,
            timeout=remaining,
        )

    try:
        # Use the container's native temporary filesystem rather than a potentially
        # slow host bind mount; both converted files are disposable.
        with tempfile.TemporaryDirectory(prefix="dosctl-extract-") as work:
            raw = Path(work) / "disk.raw"
            partition = Path(work) / "partition.img"
            run(["qemu-img", "convert", "-f", "qcow2", "-O", "raw", str(vm.disk_path), str(raw)])
            table = run(
                ["parted", "-m", "-s", str(raw), "unit", "s", "print"],
                capture=True,
            ).stdout
            line = next((item for item in table.splitlines() if item.startswith("1:")), "")
            match = re.match(r"1:(\d+)s:(\d+)s:(\d+)s:", line)
            if not match:
                raise DosVMError(f"could not parse partition table for run {vm.run_id}: {line!r}")
            start, length = int(match.group(1)), int(match.group(3))
            run([
                "dd", f"if={raw}", f"of={partition}", "bs=4M",
                "iflag=skip_bytes,count_bytes", f"skip={start * 512}",
                f"count={length * 512}", "conv=sparse", "status=none",
            ])
            guest = dos_path.replace("\\", "/")
            if len(guest) >= 2 and guest[1] == ":":
                guest = guest[2:]
            guest = guest.lstrip("/")
            run(["mcopy", "-i", str(partition), f"::/{guest}", str(temporary)])
    except subprocess.TimeoutExpired as exc:
        temporary.unlink(missing_ok=True)
        raise vm.timeout_error(
            f"post-mortem collect {dos_path!r}",
            timeout,
            detail=f"timed out running {exc.cmd!r}",
        ) from exc
    temporary.replace(destination)
    return destination


def collect_file(vm: DosVM, dos_path: str, destination: str | Path, timeout: float = 30.0) -> Path:
    _safe_path(dos_path)
    if not vm.is_alive():
        return extract_file(vm, dos_path, destination, timeout=timeout)
    destination = Path(destination).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    temporary.unlink(missing_ok=True)

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", 0))
    listener.listen(1)
    listener.settimeout(timeout)
    port = listener.getsockname()[1]
    errors: list[BaseException] = []

    def receive() -> None:
        try:
            connection, _ = listener.accept()
            with connection, temporary.open("wb") as output:
                connection.settimeout(timeout)
                while True:
                    chunk = connection.recv(65536)
                    if not chunk:
                        break
                    output.write(chunk)
        except BaseException as exc:
            errors.append(exc)
        finally:
            listener.close()

    thread = threading.Thread(target=receive, name="dos-collect", daemon=True)
    thread.start()
    started = time.monotonic()
    try:
        # This is the syntax of the pinned 2025-01-10 mTCP NC release. -bin is
        # required to prevent DOS text translation, including for empty files.
        vm.exec(f"NC -target 10.0.2.2 {port} -bin < {dos_path}", timeout=timeout)
    except Exception:
        listener.close()
        thread.join(1.0)
        temporary.unlink(missing_ok=True)
        raise
    remaining = max(0.0, timeout - (time.monotonic() - started))
    thread.join(remaining)
    if thread.is_alive():
        listener.close()
        thread.join(1.0)
        temporary.unlink(missing_ok=True)
        try:
            last_screen = vm.screen_text()
        except Exception:
            last_screen = ""
        raise vm.timeout_error(
            f"collect {dos_path!r} through mTCP Netcat",
            timeout,
            last_screen,
        )
    if errors:
        temporary.unlink(missing_ok=True)
        if isinstance(errors[0], (socket.timeout, TimeoutError)):
            try:
                last_screen = vm.screen_text()
            except Exception:
                last_screen = ""
            raise vm.timeout_error(
                f"collect {dos_path!r} through mTCP Netcat",
                timeout,
                last_screen,
                str(errors[0]),
            )
        raise DosVMError(f"mTCP Netcat receive failed: {errors[0]}")
    if not temporary.exists():
        raise DosVMError("mTCP Netcat completed without creating an output file")
    temporary.replace(destination)
    return destination
