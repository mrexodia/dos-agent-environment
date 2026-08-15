"""Small text-mode Links driver built only on the generic DosVM API."""

from __future__ import annotations

import time

from harness.dosvm import DosVM


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
        self.vm.key("g")
        self.vm.type(url)
        self.vm.key("ENTER")
        if marker:
            self.vm.wait_for(marker, timeout=timeout)

    def read_full_page(self, max_pages: int = 100) -> str:
        pages: list[str] = []
        previous = None
        for _ in range(max_pages):
            current = self.vm.screen_text()
            if current == previous:
                break
            pages.append(current)
            previous = current
            self.vm.key("PGDN")
            time.sleep(0.05)
        return "\n\f\n".join(pages)

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
