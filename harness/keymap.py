"""US keyboard mapping for QMP send-key."""

from __future__ import annotations

import time
from typing import Iterable

from .qmp import QMPClient


class KeyMappingError(ValueError):
    pass


MODIFIER_KEYS = {"SHIFT": "shift", "CTRL": "ctrl", "CONTROL": "ctrl", "ALT": "alt"}

NAMED_KEYS = {
    "ENTER": "ret",
    "RETURN": "ret",
    "ESC": "esc",
    "ESCAPE": "esc",
    "TAB": "tab",
    "BACKSPACE": "backspace",
    "SPACE": "spc",
    "UP": "up",
    "DOWN": "down",
    "LEFT": "left",
    "RIGHT": "right",
    "HOME": "home",
    "END": "end",
    "PGUP": "pgup",
    "PAGEUP": "pgup",
    "PGDN": "pgdn",
    "PAGEDOWN": "pgdn",
    "INSERT": "insert",
    "INS": "insert",
    "DELETE": "delete",
    "DEL": "delete",
}
NAMED_KEYS.update({f"F{number}": f"f{number}" for number in range(1, 13)})

_BASE_PUNCTUATION = {
    " ": "spc",
    "'": "apostrophe",
    ",": "comma",
    "-": "minus",
    ".": "dot",
    "/": "slash",
    ";": "semicolon",
    "=": "equal",
    "[": "bracket_left",
    "\\": "backslash",
    "]": "bracket_right",
    "`": "grave_accent",
}
_SHIFTED = {
    "!": "1",
    '"': "apostrophe",
    "#": "3",
    "$": "4",
    "%": "5",
    "&": "7",
    "(": "9",
    ")": "0",
    "*": "8",
    "+": "equal",
    ":": "semicolon",
    "<": "comma",
    ">": "dot",
    "?": "slash",
    "@": "2",
    "^": "6",
    "_": "minus",
    "{": "bracket_left",
    "|": "backslash",
    "}": "bracket_right",
    "~": "grave_accent",
}


def chord_for_character(character: str) -> list[str]:
    if len(character) != 1:
        raise KeyMappingError(f"expected one character, got {character!r}")
    if "a" <= character <= "z" or "0" <= character <= "9":
        return [character]
    if "A" <= character <= "Z":
        return ["shift", character.lower()]
    if character in _BASE_PUNCTUATION:
        return [_BASE_PUNCTUATION[character]]
    if character in _SHIFTED:
        return ["shift", _SHIFTED[character]]
    if character in "\r\n":
        return ["ret"]
    if character == "\t":
        return ["tab"]
    raise KeyMappingError(f"character is not available in the DOS US keymap: {character!r}")


def send_chord(qmp: QMPClient, qcodes: Iterable[str], hold_ms: int = 20) -> None:
    keys = [{"type": "qcode", "data": qcode} for qcode in qcodes]
    qmp.execute("send-key", {"keys": keys, "hold-time": hold_ms})


def chord_for_named_key(name: str) -> list[str]:
    """Map a key name or modifier chord such as CTRL_C or SHIFT+TAB."""
    normalized = name.upper().replace("+", "_")
    parts = normalized.split("_")
    if len(parts) > 1:
        if all(part in MODIFIER_KEYS for part in parts[:-1]):
            modifiers = [MODIFIER_KEYS[part] for part in parts[:-1]]
            tail = parts[-1]
            qcode = NAMED_KEYS.get(tail)
            if qcode is None and len(tail) == 1 and tail.isalnum():
                qcode = tail.lower()
            if qcode is not None:
                return [*modifiers, qcode]
    qcode = NAMED_KEYS.get(normalized)
    if qcode is not None:
        return [qcode]
    if len(name) == 1:
        return chord_for_character(name)
    raise KeyMappingError(f"unknown named key: {name}")


def send_named_key(qmp: QMPClient, name: str, delay: float = 0.04) -> None:
    send_chord(qmp, chord_for_named_key(name))
    if delay:
        time.sleep(delay)


def type_text(qmp: QMPClient, text: str, delay: float = 0.04) -> None:
    previous_was_cr = False
    for character in text:
        if character == "\n" and previous_was_cr:
            previous_was_cr = False
            continue
        send_chord(qmp, chord_for_character(character))
        if delay:
            time.sleep(delay)
        previous_was_cr = character == "\r"
