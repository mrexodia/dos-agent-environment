import pytest

from harness.dosvm import DosVM, DosVMError


def test_invalid_wait_regex_is_reported_without_entering_poll_loop(tmp_path):
    vm = DosVM(tmp_path / "invalid-regex")
    with pytest.raises(DosVMError, match="invalid screen regular expression"):
        vm.wait_for(r"C:\PERSIA>")


def test_dosctl_run_id_environment_selects_run(tmp_path, monkeypatch):
    runs = tmp_path / "build" / "runs"
    (runs / "selected").mkdir(parents=True)
    monkeypatch.setenv("DOSDEV_ROOT", str(tmp_path))
    monkeypatch.setenv("DOSCTL_RUN_ID", "selected")
    assert DosVM.current().run_id == "selected"
