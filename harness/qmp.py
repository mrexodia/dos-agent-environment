"""Minimal synchronous QEMU Machine Protocol client."""

from __future__ import annotations

import json
import socket
import time
from pathlib import Path
from typing import Any


class QMPError(RuntimeError):
    pass


class QMPClient:
    def __init__(self, path: str | Path, timeout: float = 5.0):
        self.path = str(path)
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self.stream = None
        self._next_id = 1
        self.events: list[dict[str, Any]] = []

    def connect(self) -> "QMPClient":
        if self.sock is not None:
            return self
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(self.timeout)
        sock.connect(self.path)
        self.sock = sock
        self.stream = sock.makefile("rwb", buffering=0)
        greeting = self._read_message(time.monotonic() + self.timeout)
        if "QMP" not in greeting:
            self.close()
            raise QMPError(f"invalid QMP greeting: {greeting!r}")
        self.execute("qmp_capabilities")
        return self

    def close(self) -> None:
        if self.stream is not None:
            try:
                self.stream.close()
            except OSError:
                pass
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.stream = None
        self.sock = None

    def __enter__(self) -> "QMPClient":
        return self.connect()

    def __exit__(self, *_: object) -> None:
        self.close()

    def _read_message(self, deadline: float) -> dict[str, Any]:
        if self.sock is None or self.stream is None:
            raise QMPError("QMP client is not connected")
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for QMP response")
            self.sock.settimeout(remaining)
            try:
                line = self.stream.readline()
            except socket.timeout as exc:
                raise TimeoutError("timed out waiting for QMP response") from exc
            if not line:
                raise QMPError("QMP connection closed")
            try:
                message = json.loads(line)
            except json.JSONDecodeError as exc:
                raise QMPError(f"invalid QMP JSON: {line!r}") from exc
            if isinstance(message, dict):
                return message

    def execute(
        self,
        command: str,
        arguments: dict[str, Any] | None = None,
        timeout: float | None = None,
    ) -> Any:
        if self.sock is None or self.stream is None:
            self.connect()
        assert self.stream is not None
        request_id = self._next_id
        self._next_id += 1
        request: dict[str, Any] = {"execute": command, "id": request_id}
        if arguments:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request, separators=(",", ":")).encode() + b"\r\n")
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        while True:
            message = self._read_message(deadline)
            if "event" in message:
                self.events.append(message)
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                error = message["error"]
                raise QMPError(
                    f"QMP {command} failed: {error.get('class', 'Error')}: "
                    f"{error.get('desc', error)!s}"
                )
            return message.get("return")

    def hmp(self, command: str, timeout: float | None = None) -> str:
        result = self.execute(
            "human-monitor-command",
            {"command-line": command},
            timeout=timeout,
        )
        if not isinstance(result, str):
            raise QMPError(f"unexpected HMP result for {command!r}: {result!r}")
        return result
