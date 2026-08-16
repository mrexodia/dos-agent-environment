"""VGA text extraction and QEMU display screenshots."""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .qmp import QMPClient, QMPError

_HEX_BYTE = re.compile(r"0x([0-9a-fA-F]{2})(?![0-9a-fA-F])")
_ASCII_GLYPHS = str.maketrans({
    "─": "-", "━": "-", "═": "-",
    "│": "|", "┃": "|", "║": "|",
    "┌": "+", "┐": "+", "└": "+", "┘": "+",
    "╔": "+", "╗": "+", "╚": "+", "╝": "+",
    "├": "+", "┤": "+", "┬": "+", "┴": "+", "┼": "+",
    "╠": "+", "╣": "+", "╦": "+", "╩": "+", "╬": "+",
    "░": ".", "▒": ":", "▓": "#", "█": "#", "▄": "#", "▀": "#",
    "←": "<", "→": ">", "↑": "^", "↓": "v", "•": "*",
})


class UnsupportedVideoMode(RuntimeError):
    pass


@dataclass(frozen=True)
class Cell:
    character: str
    attribute: int

    def as_dict(self) -> dict[str, Any]:
        return {
            "character": self.character,
            "attribute": self.attribute,
            "foreground": self.attribute & 0x0F,
            "background": (self.attribute >> 4) & 0x07,
            "blink": bool(self.attribute & 0x80),
        }


@dataclass(frozen=True)
class TextScreen:
    mode: int
    columns: int
    rows: int
    page_offset: int
    cells: tuple[tuple[Cell, ...], ...]

    def text(self, trim_blank_rows: bool = False) -> str:
        lines = ["".join(cell.character for cell in row).rstrip() for row in self.cells]
        if trim_blank_rows:
            while lines and not lines[-1]:
                lines.pop()
            while lines and not lines[0]:
                lines.pop(0)
        return "\n".join(lines)

    def ascii(self, trim_blank_rows: bool = False) -> str:
        text = self.text(trim_blank_rows=trim_blank_rows).translate(_ASCII_GLYPHS)
        normalized = unicodedata.normalize("NFKD", text)
        return normalized.encode("ascii", errors="replace").decode("ascii")

    def as_dict(self) -> dict[str, Any]:
        return {
            "mode": self.mode,
            "columns": self.columns,
            "rows": self.rows,
            "page_offset": self.page_offset,
            "text": self.text(),
            "cells": [[cell.as_dict() for cell in row] for row in self.cells],
        }


def read_physical(qmp: QMPClient, address: int, count: int) -> bytes:
    if count <= 0:
        return b""
    output = qmp.hmp(f"xp /{count}bx 0x{address:x}", timeout=max(5.0, count / 500.0))
    values: list[int] = []
    for line in output.splitlines():
        _, separator, data = line.partition(":")
        if not separator:
            continue
        values.extend(int(match, 16) for match in _HEX_BYTE.findall(data))
    if len(values) != count:
        raise QMPError(
            f"memory read at 0x{address:x} returned {len(values)} bytes, expected {count}"
        )
    return bytes(values)


def read_text_screen(qmp: QMPClient) -> TextScreen:
    bda = read_physical(qmp, 0x400, 0x100)
    mode = bda[0x49]
    if mode != 0x03:
        raise UnsupportedVideoMode(
            f"text scraping supports VGA color mode 0x03; current mode is 0x{mode:02x}"
        )
    columns = int.from_bytes(bda[0x4A:0x4C], "little")
    page_offset = int.from_bytes(bda[0x4E:0x50], "little")
    reported_rows = bda[0x84] + 1
    if not 20 <= columns <= 160:
        columns = 80
    if not 20 <= reported_rows <= 60:
        reported_rows = 25
    rows = reported_rows
    raw = read_physical(qmp, 0xB8000 + page_offset, columns * rows * 2)
    grid: list[tuple[Cell, ...]] = []
    for row_number in range(rows):
        row: list[Cell] = []
        base = row_number * columns * 2
        for column in range(columns):
            character = bytes([raw[base + column * 2]]).decode("cp437")
            attribute = raw[base + column * 2 + 1]
            row.append(Cell(character, attribute))
        grid.append(tuple(row))
    return TextScreen(mode, columns, rows, page_offset, tuple(grid))


def screenshot(qmp: QMPClient, output: str | Path) -> Path:
    from PIL import Image

    output_path = Path(output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    ppm_path = output_path.with_suffix(output_path.suffix + ".ppm")
    qmp.execute("screendump", {"filename": str(ppm_path)}, timeout=10.0)
    try:
        with Image.open(ppm_path) as image:
            image.save(output_path, format="PNG")
    finally:
        ppm_path.unlink(missing_ok=True)
    return output_path
