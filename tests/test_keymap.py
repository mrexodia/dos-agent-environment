import string

import pytest

from harness.keymap import (
    KeyMappingError,
    chord_for_character,
    chord_for_named_key,
    send_key_state,
    send_named_key,
)


def test_all_printable_ascii_is_mapped():
    for character in string.printable:
        if character in "\x0b\x0c":
            continue
        assert chord_for_character(character)


def test_uppercase_uses_shift():
    assert chord_for_character("A") == ["shift", "a"]


def test_named_navigation_and_modifier_chords():
    assert chord_for_named_key("PGDN") == ["pgdn"]
    assert chord_for_named_key("CTRL") == ["ctrl"]
    assert chord_for_named_key("CTRL_C") == ["ctrl", "c"]
    assert chord_for_named_key("shift+tab") == ["shift", "tab"]
    assert chord_for_named_key("SHIFT_END") == ["shift", "end"]


def test_unknown_unicode_is_rejected():
    with pytest.raises(KeyMappingError):
        chord_for_character("é")
    with pytest.raises(KeyMappingError):
        chord_for_named_key("HYPERDRIVE")


def test_tap_exposes_hold_duration():
    class FakeQMP:
        def __init__(self):
            self.request = None

        def execute(self, command, arguments):
            self.request = (command, arguments)

    qmp = FakeQMP()
    send_named_key(qmp, "RIGHT", delay=0, hold_ms=1500)
    assert qmp.request == (
        "send-key",
        {
            "keys": [{"type": "qcode", "data": "right"}],
            "hold-time": 1500,
        },
    )


def test_key_state_releases_chord_in_reverse_order():
    class FakeQMP:
        def __init__(self):
            self.request = None

        def execute(self, command, arguments):
            self.request = (command, arguments)

    qmp = FakeQMP()
    send_key_state(qmp, ["ctrl", "q"], down=False)
    assert qmp.request[0] == "input-send-event"
    events = qmp.request[1]["events"]
    assert [event["data"]["key"]["data"] for event in events] == ["q", "ctrl"]
    assert all(event["data"]["down"] is False for event in events)
