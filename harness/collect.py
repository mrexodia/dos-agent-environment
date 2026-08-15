"""Transfer a guest file to the host with mTCP Netcat."""

from __future__ import annotations

import socket
import threading
from pathlib import Path

from .dosvm import DosTimeout, DosVM, DosVMError


def collect_file(vm: DosVM, dos_path: str, destination: str | Path, timeout: float = 30.0) -> Path:
    if not dos_path or any(character in dos_path for character in '<>|&"\r\n'):
        raise DosVMError(f"unsafe DOS path for collection: {dos_path!r}")
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
    try:
        vm.exec(f"NC -target 10.0.2.2 {port} -bin < {dos_path}", timeout=timeout)
    except Exception:
        listener.close()
        thread.join(1.0)
        temporary.unlink(missing_ok=True)
        raise
    thread.join(timeout)
    if thread.is_alive():
        listener.close()
        temporary.unlink(missing_ok=True)
        raise DosTimeout("timed out waiting for mTCP Netcat transfer to finish")
    if errors:
        temporary.unlink(missing_ok=True)
        raise DosVMError(f"mTCP Netcat receive failed: {errors[0]}")
    if not temporary.exists():
        raise DosVMError("mTCP Netcat completed without creating an output file")
    temporary.replace(destination)
    return destination
