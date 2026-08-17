PYTHON ?= python3

.PHONY: payload image runtime start stop smoke unit integration test toolchain-smoke clean

payload: payload/BIN/HELLO.COM payload/BIN/MAKEBIN.COM payload/BIN/LINKS.EXE

payload/BIN/HELLO.COM: apps/smoke/hello.asm
	mkdir -p payload/BIN
	nasm -f bin -o $@ $<

payload/BIN/MAKEBIN.COM: apps/smoke/makebinary.asm
	mkdir -p payload/BIN
	nasm -f bin -o $@ $<

payload/BIN/LINKS.EXE: inputs/links-2.30.exe
	mkdir -p payload/BIN
	cp $< $@

image: payload
	./scripts/build-image.sh

runtime: payload
	./scripts/build-runtime.sh

start:
	./dosctl start

stop:
	./dosctl stop --all

smoke: runtime
	$(PYTHON) scripts/smoke.py

unit:
	$(PYTHON) -m pytest -q

integration: smoke
	DOS_LINKS_INTEGRATION=1 $(PYTHON) -m pytest -q

test: integration

toolchain-smoke:
	./scripts/toolchain-smoke.sh

clean:
	./dosctl stop --all 2>/dev/null || true
	rm -rf build
