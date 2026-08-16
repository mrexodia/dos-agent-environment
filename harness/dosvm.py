"""QEMU process lifecycle and high-level DOS operations."""

from __future__ import annotations

import json
import os
import re
import secrets
import signal
import socket
import subprocess
import time
from pathlib import Path
from typing import Any, Pattern

from .keymap import send_named_key, type_text
from .qmp import QMPClient, QMPError
from .screen import TextScreen, UnsupportedVideoMode, read_text_screen, screenshot


class DosVMError(RuntimeError):
    pass


class DosTimeout(DosVMError):
    pass


def project_root() -> Path:
    override = os.environ.get("DOSDEV_ROOT")
    return Path(override).resolve() if override else Path(__file__).resolve().parents[1]


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except (ProcessLookupError, PermissionError):
        return False
    # An orphaned QEMU can briefly remain as a zombie in minimal containers;
    # kill(0) still succeeds for zombies, but there is nothing left to stop.
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().split()
        if len(fields) > 2 and fields[2] == "Z":
            return False
    except OSError:
        pass
    return True


def _process_command(pid: int) -> str:
    try:
        return Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").decode(
            errors="replace"
        )
    except OSError:
        try:
            return subprocess.check_output(
                ["ps", "-p", str(pid), "-o", "command="], text=True
            ).strip()
        except Exception:
            return ""


class DosVM:
    def __init__(self, run_dir: str | Path):
        self.root = project_root()
        self.run_dir = Path(run_dir).resolve()
        self.run_id = self.run_dir.name
        self.qmp_path = self.run_dir / "qmp.sock"
        self.serial_path = self.run_dir / "serial.sock"
        self.pid_path = self.run_dir / "qemu.pid"
        self.log_path = self.run_dir / "qemu.log"
        self.disk_path = self.run_dir / "disk.qcow2"

    @classmethod
    def current(cls, run_id: str | None = None) -> "DosVM":
        root = project_root()
        runs = root / "build" / "runs"
        if run_id:
            run_dir = runs / run_id
        else:
            current_file = runs / "current"
            if not current_file.exists():
                raise DosVMError("no current DOS VM; run dosctl start")
            selected = current_file.read_text().strip()
            if not selected:
                raise DosVMError("current run file is empty")
            run_dir = runs / selected
        if not run_dir.is_dir():
            raise DosVMError(f"DOS run does not exist: {run_dir}")
        return cls(run_dir)

    @classmethod
    def start(
        cls,
        run_id: str | None = None,
        wait_for_prompt: bool = True,
        timeout: float = 30.0,
    ) -> "DosVM":
        root = project_root()
        base = root / "build" / "dos71.qcow2"
        if not base.exists():
            raise DosVMError("build/dos71.qcow2 is missing; run make runtime")
        if run_id is None:
            run_id = time.strftime("%Y%m%d-%H%M%S") + f"-{os.getpid()}-{secrets.token_hex(2)}"
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", run_id):
            raise DosVMError("run ID may contain only letters, digits, dot, underscore, and dash")
        runs = root / "build" / "runs"
        run_dir = runs / run_id
        if run_dir.exists():
            raise DosVMError(f"run already exists: {run_id}")
        run_dir.mkdir(parents=True)
        vm = cls(run_dir)
        try:
            subprocess.run(
                [
                    "qemu-img",
                    "create",
                    "-q",
                    "-f",
                    "qcow2",
                    "-F",
                    "qcow2",
                    "-b",
                    str(base.resolve()),
                    str(vm.disk_path),
                ],
                check=True,
            )
            command = [
                "qemu-system-i386",
                "-accel",
                "tcg",
                "-m",
                "64",
                "-boot",
                "order=c",
                "-drive",
                f"file={vm.disk_path},format=qcow2,if=none,id=dosdisk",
                "-device",
                "ide-hd,drive=dosdisk,bus=ide.0,unit=0,cyls=1024,heads=16,secs=63",
                "-nic",
                "user,model=pcnet",
                "-qmp",
                f"unix:{vm.qmp_path},server=on,wait=off",
                "-serial",
                f"unix:{vm.serial_path},server=on,wait=off",
                "-display",
                "none",
                "-no-reboot",
            ]
            with vm.log_path.open("wb") as log:
                process = subprocess.Popen(
                    command,
                    cwd=root,
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
            vm.pid_path.write_text(f"{process.pid}\n")
            vm._wait_for_qmp(min(timeout, 10.0))
            (runs / "current").write_text(f"{run_id}\n")
            if wait_for_prompt:
                vm.wait_for_prompt(timeout=timeout)
            return vm
        except Exception:
            try:
                vm.stop(force=True)
            except Exception:
                pass
            raise

    def _wait_for_qmp(self, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            if self.pid_path.exists() and not self.is_alive():
                log = self.log_path.read_text(errors="replace") if self.log_path.exists() else ""
                raise DosVMError(f"QEMU exited during startup\n{log[-4000:]}")
            try:
                with self.qmp():
                    return
            except (OSError, QMPError, TimeoutError) as exc:
                last_error = exc
                time.sleep(0.05)
        raise self.timeout_error(
            "wait for QMP greeting and capabilities handshake",
            timeout,
            detail=str(last_error) if last_error else None,
        )

    def qmp(self, timeout: float = 5.0) -> QMPClient:
        return QMPClient(self.qmp_path, timeout=timeout)

    def pid(self) -> int | None:
        try:
            value = int(self.pid_path.read_text().strip())
            return value if value > 0 else None
        except (OSError, ValueError):
            return None

    def is_alive(self) -> bool:
        pid = self.pid()
        if pid is None or not _pid_alive(pid):
            return False
        command = _process_command(pid)
        return "qemu-system" in command and str(self.disk_path) in command

    def status(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "run_id": self.run_id,
            "pid": self.pid(),
            "alive": self.is_alive(),
            "run_dir": str(self.run_dir),
        }
        if result["alive"] and self.qmp_path.exists():
            try:
                with self.qmp(timeout=1.0) as qmp:
                    result["qemu"] = qmp.execute("query-status", timeout=1.0)
            except Exception as exc:
                result["qmp_error"] = str(exc)
        return result

    def timeout_error(
        self,
        operation: str,
        timeout: float,
        last_screen: str = "",
        detail: str | None = None,
    ) -> DosTimeout:
        """Build the mandatory diagnostic-rich timeout used by public operations."""
        status = self.status()
        screenshot_path: Path | None = None
        if status.get("alive"):
            try:
                screenshot_path = self.screenshot(self.run_dir / "timeout.png")
            except Exception:
                pass
        sections = [
            f"timed out after {timeout:.1f}s in run {self.run_id}",
            f"operation: {operation}",
            f"QEMU status: {json.dumps(status, sort_keys=True)}",
        ]
        if detail:
            sections.append(f"detail: {detail}")
        if screenshot_path:
            sections.append(f"screenshot: {screenshot_path}")
        sections.append(f"--- last screen ---\n{last_screen}")
        return DosTimeout("\n".join(sections))

    def screen(self) -> TextScreen:
        if not self.is_alive():
            raise DosVMError(f"DOS VM {self.run_id} is not running")
        with self.qmp(timeout=10.0) as qmp:
            return read_text_screen(qmp)

    def screen_text(self) -> str:
        return self.screen().text()

    def wait_for(
        self,
        pattern: str | Pattern[str],
        timeout: float = 10.0,
        require_change_from: str | None = None,
        interval: float = 0.1,
    ) -> TextScreen:
        regex = re.compile(pattern) if isinstance(pattern, str) else pattern
        deadline = time.monotonic() + timeout
        last_text = ""
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            if not self.is_alive():
                raise DosVMError(f"QEMU stopped while waiting for {regex.pattern!r}")
            try:
                current = self.screen()
                last_text = current.text()
                if regex.search(last_text) and (
                    require_change_from is None or last_text != require_change_from
                ):
                    return current
            except (UnsupportedVideoMode, QMPError, OSError, TimeoutError) as exc:
                last_error = exc
            time.sleep(interval)
        detail = f"; last screen error: {last_error}" if last_error else ""
        raise self.timeout_error(
            f"wait for screen regex {regex.pattern!r}",
            timeout,
            last_text,
            detail.lstrip("; ") or None,
        )

    def poll_for_screen_change(
        self,
        previous: str | TextScreen,
        timeout: float = 2.0,
        interval: float = 0.05,
    ) -> TextScreen | None:
        """Return a new character/attribute generation, or None if still stable."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.is_alive():
                raise DosVMError("QEMU stopped while observing the screen")
            try:
                current = self.screen()
                current_text = current.text()
                changed = current != previous if isinstance(previous, TextScreen) else current_text != previous
                if changed:
                    return current
            except (UnsupportedVideoMode, QMPError, OSError, TimeoutError):
                pass
            time.sleep(interval)
        return None

    def wait_for_screen_change(
        self,
        previous: str | TextScreen,
        timeout: float = 2.0,
        interval: float = 0.05,
    ) -> TextScreen:
        """Wait for new characters or attributes instead of sleeping blindly."""
        current = self.poll_for_screen_change(previous, timeout, interval)
        if current is not None:
            return current
        try:
            last_text = self.screen_text()
        except Exception:
            last_text = previous.text() if isinstance(previous, TextScreen) else previous
        raise self.timeout_error(
            "wait for a new screen generation",
            timeout,
            last_text,
        )

    def wait_for_prompt(
        self,
        timeout: float = 10.0,
        token: str | None = None,
        require_change_from: str | None = None,
    ) -> TextScreen:
        prefix = re.escape(token) if token else ""
        prompt = re.compile(prefix + r"[A-Z]:\\[^>\r\n]*>")
        deadline = time.monotonic() + timeout
        last_text = ""
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            if not self.is_alive():
                raise DosVMError("QEMU stopped while waiting for the active DOS prompt")
            try:
                current = self.screen()
                last_text = current.text()
                lines = [line.rstrip() for line in last_text.splitlines() if line.rstrip()]
                if (
                    lines
                    and prompt.fullmatch(lines[-1])
                    and (require_change_from is None or last_text != require_change_from)
                ):
                    return current
            except (UnsupportedVideoMode, QMPError, OSError, TimeoutError) as exc:
                last_error = exc
            time.sleep(0.1)
        detail = f"; last screen error: {last_error}" if last_error else ""
        raise self.timeout_error(
            "wait for the newly active bottom DOS prompt",
            timeout,
            last_text,
            detail.lstrip("; ") or None,
        )

    def type(self, text: str, delay: float = 0.04) -> None:
        with self.qmp(timeout=max(5.0, len(text) * (delay + 0.1))) as qmp:
            type_text(qmp, text, delay=delay)

    def key(self, *names: str, delay: float = 0.04) -> None:
        with self.qmp(timeout=max(5.0, len(names) * 0.2)) as qmp:
            for name in names:
                send_named_key(qmp, name, delay=delay)

    def screenshot(self, output: str | Path | None = None) -> Path:
        if output is None:
            output = self.run_dir / "screen.png"
        with self.qmp(timeout=10.0) as qmp:
            return screenshot(qmp, output)

    def exec(self, command: str, timeout: float = 30.0) -> str:
        """Execute a short shell command on VGA and return its visible output.

        This is the intuitive default: the command and output remain on screen.
        Use exec_serial() when output may exceed the text-screen height.
        """
        if not command or "\r" in command or "\n" in command:
            raise DosVMError("exec requires one non-empty DOS command line")

        initial_screen = self.wait_for_prompt(timeout=min(timeout, 10.0))
        initial = initial_screen.text()
        initial_nonempty = [line.rstrip() for line in initial.splitlines() if line.rstrip()]
        if len(initial_nonempty) == 1:
            clean_screen = initial_screen
        else:
            self.type("CLS\r")
            clean_screen = self.wait_for_prompt(
                timeout=5.0, require_change_from=initial
            )
        clean_text = clean_screen.text()
        clean_lines = [line.rstrip() for line in clean_text.splitlines()]
        clean_nonempty = [line for line in clean_lines if line]
        if not clean_nonempty:
            raise DosVMError("could not identify the DOS prompt after CLS")
        prompt = clean_nonempty[-1]

        self.type(command + "\r")
        completed = self.wait_for_prompt(
            timeout=timeout, require_change_from=clean_text
        )
        lines = [line.rstrip() for line in completed.text().splitlines()]
        nonempty_indexes = [index for index, line in enumerate(lines) if line]
        if not nonempty_indexes:
            return ""
        first = nonempty_indexes[0]
        last = nonempty_indexes[-1]
        expected_command = prompt + command
        if lines[first] != expected_command:
            # Commands such as CLS intentionally erase their own command line.
            if len(nonempty_indexes) == 1 and re.fullmatch(
                r"[A-Z]:\\[^>\r\n]*>", lines[last]
            ):
                return ""
            raise DosVMError(
                "command output scrolled beyond the VGA screen; rerun with "
                "'dosctl exec --serial' for complete capture\n"
                f"--- last screen ---\n{completed.text()}"
            )
        output = lines[first + 1 : last]
        while output and not output[0]:
            output.pop(0)
        while output and not output[-1]:
            output.pop()
        return "\n".join(output)

    def _connect_serial(self, timeout: float = 5.0) -> socket.socket:
        deadline = time.monotonic() + timeout
        last_error: OSError | None = None
        while time.monotonic() < deadline:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(min(0.5, max(0.05, deadline - time.monotonic())))
            try:
                sock.connect(str(self.serial_path))
                sock.settimeout(None)
                return sock
            except OSError as exc:
                last_error = exc
                sock.close()
                time.sleep(0.05)
        try:
            last_screen = self.screen_text()
        except Exception:
            last_screen = ""
        raise self.timeout_error(
            "connect to the per-run serial console",
            timeout,
            last_screen,
            str(last_error) if last_error else None,
        )

    def _receive_until(
        self,
        sock: socket.socket,
        regex: Pattern[bytes],
        timeout: float,
    ) -> tuple[bytes, re.Match[bytes]]:
        deadline = time.monotonic() + timeout
        data = bytearray()
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            sock.settimeout(min(0.25, remaining))
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                raise DosVMError("serial console closed")
            data.extend(chunk)
            match = regex.search(data)
            if match:
                return bytes(data), match
        received = bytes(data).decode("cp437", errors="replace")
        try:
            last_screen = self.screen_text()
        except Exception:
            last_screen = ""
        raise self.timeout_error(
            f"serial receive waiting for {regex.pattern!r}",
            timeout,
            last_screen,
            f"serial data received: {received!r}",
        )

    def exec_serial(self, command: str, timeout: float = 30.0) -> str:
        """Execute a non-interactive command with unbounded serial capture."""
        if not command or "\r" in command or "\n" in command:
            raise DosVMError("exec requires one non-empty DOS command line")
        first_word = command.lstrip().split(maxsplit=1)[0].upper()
        if first_word in {"CTTY", "PROMPT", "COMMAND"}:
            raise DosVMError(f"exec does not allow console-control command {first_word}")

        initial = self.wait_for_prompt(timeout=min(timeout, 10.0)).text()
        token = f"DOSCTL{secrets.token_hex(4).upper()}"
        self.type(f"PROMPT {token}$P$G\r")
        self.wait_for_prompt(timeout=5.0, token=token, require_change_from=initial)

        serial = self._connect_serial()
        try:
            before_ctty = self.screen_text()
            self.type("CTTY COM1\r")
            prompt_bytes = re.compile(
                re.escape(token.encode("ascii")) + rb"[A-Z]:\\[^>\r\n]*>"
            )
            self._receive_until(serial, prompt_bytes, 5.0)

            serial.sendall(command.encode("cp437") + b"\r")
            raw, match = self._receive_until(serial, prompt_bytes, timeout)
            captured = raw[: match.start()].decode("cp437", errors="replace")

            serial.sendall(b"CTTY CON\r")
            self.wait_for_prompt(timeout=5.0, token=token, require_change_from=before_ctty)
        except Exception as exc:
            # CTTY ownership cannot be established after any failure. The run is
            # disposable, so enforce the contract rather than asking callers to
            # remember to clean up an ambiguous console.
            serial.close()
            try:
                self.stop(force=True)
            except Exception as stop_exc:
                raise DosVMError(
                    f"serial exec failed in run {self.run_id}, and forced cleanup "
                    f"also failed: {stop_exc}; original error: {exc}"
                ) from exc
            raise DosVMError(
                f"serial exec failed; ambiguous run {self.run_id} was stopped: {exc}"
            ) from exc
        finally:
            serial.close()

        token_screen = self.screen_text()
        self.type("PROMPT $P$G\r")
        restored = self.wait_for_prompt(
            timeout=5.0, require_change_from=token_screen
        ).text()
        # `exec` returns its output over serial, so its temporary PROMPT/CTTY
        # plumbing is implementation detail rather than useful VGA history.
        self.type("CLS\r")
        self.wait_for_prompt(timeout=5.0, require_change_from=restored)

        normalized = captured.replace("\r\n", "\n").replace("\r", "\n")
        lines = normalized.splitlines()
        if lines and lines[0].strip().upper() == command.strip().upper():
            lines.pop(0)
        while lines and not lines[0]:
            lines.pop(0)
        while lines and not lines[-1]:
            lines.pop()
        return "\n".join(lines)

    def stop(self, force: bool = False, timeout: float = 5.0) -> None:
        """Quit, terminate, then kill with bounded waits and PID verification."""
        pid = self.pid()
        if pid is None or not self.is_alive():
            self._clear_current()
            return
        try:
            with self.qmp(timeout=1.0) as qmp:
                qmp.execute("quit", timeout=1.0)
        except Exception:
            pass
        deadline = time.monotonic() + (0.25 if force else timeout)
        while _pid_alive(pid) and time.monotonic() < deadline:
            time.sleep(0.05)
        if _pid_alive(pid):
            command = _process_command(pid)
            if "qemu-system" not in command or str(self.disk_path) not in command:
                raise DosVMError(
                    f"refusing to signal pid {pid}; it is not the recorded QEMU process: {command!r}"
                )
            os.killpg(pid, signal.SIGTERM)
            deadline = time.monotonic() + 2.0
            while _pid_alive(pid) and time.monotonic() < deadline:
                time.sleep(0.05)
            if _pid_alive(pid):
                os.killpg(pid, signal.SIGKILL)
                deadline = time.monotonic() + 2.0
                while _pid_alive(pid) and time.monotonic() < deadline:
                    time.sleep(0.05)
                if _pid_alive(pid):
                    raise DosVMError(f"QEMU pid {pid} survived SIGKILL")
        self._clear_current()

    def _clear_current(self) -> None:
        current = self.root / "build" / "runs" / "current"
        try:
            if current.read_text().strip() == self.run_id:
                current.unlink()
        except OSError:
            pass


def all_runs() -> list[DosVM]:
    runs = project_root() / "build" / "runs"
    if not runs.exists():
        return []
    return [DosVM(path) for path in sorted(runs.iterdir()) if path.is_dir()]
