"""Text-mode Links driver built only on the public, generic DosVM API."""

from __future__ import annotations

from harness.dosvm import DosVM, DosVMError


class LinksDriver:
    def __init__(self, vm: DosVM):
        self.vm = vm

    def launch(self, url: str, marker: str | None = None, timeout: float = 20.0) -> None:
        before = self.vm.screen_text()
        self.vm.type(f"LINKS {url}\r")
        pattern = r"Welcome to links!" + (f"|{marker}" if marker else "")
        screen = self.vm.wait_for(pattern, timeout=timeout, require_change_from=before)
        if "Welcome to links!" in screen.text():
            dialog = screen.text()
            self.vm.key("ENTER")
            if marker:
                self.vm.wait_for(marker, timeout=timeout, require_change_from=dialog)

    def goto(self, url: str, marker: str | None = None, timeout: float = 20.0) -> None:
        before = self.vm.screen_text()
        self.vm.key("g")
        self.vm.type(url)
        self.vm.key("ENTER")
        if marker:
            self.vm.wait_for(marker, timeout=timeout, require_change_from=before)

    def navigate(self, *keys: str) -> None:
        """Send Links navigation keys (UP, DOWN, TAB, PGDN, and so on)."""
        self.vm.key(*keys)

    def enter(self) -> None:
        self.vm.key("ENTER")

    def follow_selected(self, marker: str | None = None, timeout: float = 20.0) -> None:
        before = self.vm.screen_text()
        self.enter()
        if marker:
            self.vm.wait_for(marker, timeout=timeout, require_change_from=before)
        else:
            self.vm.wait_for_screen_change(before, timeout=timeout)

    def fill_selected(self, text: str, clear: bool = False) -> None:
        """Fill the currently selected form control without assuming VGA colors."""
        if clear:
            self.vm.key("HOME", "SHIFT_END", "BACKSPACE")
        self.vm.type(text)

    def fill_and_submit(
        self,
        text: str,
        steps_to_field: int = 1,
        steps_to_submit: int = 1,
        marker: str | None = None,
        timeout: float = 20.0,
    ) -> None:
        """Move to a text field, enter text, move to submit, and activate it."""
        before = self.vm.screen_text()
        for _ in range(steps_to_field):
            self.vm.key("DOWN")
        self.fill_selected(text)
        for _ in range(steps_to_submit):
            self.vm.key("DOWN")
        self.vm.key("ENTER")
        if marker:
            self.vm.wait_for(marker, timeout=timeout, require_change_from=before)

    def read_full_page(self, max_pages: int = 100, change_timeout: float = 0.75) -> str:
        """Page until Links no longer produces a new screen generation."""
        pages: list[str] = []
        current = self.vm.screen_text()
        for _ in range(max_pages):
            if not pages or current != pages[-1]:
                pages.append(current)
            self.vm.key("PGDN")
            changed = self.vm.poll_for_screen_change(current, timeout=change_timeout)
            if changed is None:
                return "\n\f\n".join(pages)
            current = changed.text()
        raise DosVMError(f"Links page did not stabilize within {max_pages} pages")

    def quit(self, timeout: float = 5.0) -> None:
        before = self.vm.screen_text()
        self.vm.key("q")
        screen = self.vm.wait_for(
            r"Do you really want to exit Links\?|[A-Z]:\\[^>\r\n]*>",
            timeout=timeout,
            require_change_from=before,
        )
        if "Do you really want to exit Links?" in screen.text():
            self.vm.key("ENTER")
            self.vm.wait_for_prompt(timeout=timeout)
