PYTHON ?= python3

BASE_SOURCES := \
	scripts/build-base.sh \
	inputs/SHA256SUMS \
	inputs/Windows98_SE_No_Ramdrive.img \
	inputs/pcntpk.com \
	inputs/cwsdpmi.exe \
	inputs/mTCP_2025-01-10_upx.zip \
	inputs/links-2.30.exe \
	guest/MSDOS.SYS \
	guest/CONFIG.SYS \
	guest/AUTOEXEC.BAT \
	guest/BIN/SERIAL.BAT \
	guest/MTCP/TCP.CFG

.PHONY: payload base image runtime full-rebuild start stop smoke unit integration test toolchain-smoke clean

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

base: build/dos71-base.img

build/dos71-base.img: $(BASE_SOURCES)
	./scripts/build-base.sh

image: payload base
	./scripts/build-image.sh

runtime: payload base
	./scripts/build-runtime.sh

full-rebuild:
	./dosctl stop --all 2>/dev/null || true
	rm -f build/dos71-base.img build/dos71.img build/dos71.qcow2
	$(MAKE) runtime

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
