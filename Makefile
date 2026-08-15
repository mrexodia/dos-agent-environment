PYTHON ?= python3

.PHONY: payload image runtime start stop smoke test clean

payload: payload/BIN/HELLO.COM

payload/BIN/HELLO.COM: apps/smoke/hello.asm
	mkdir -p payload/BIN
	nasm -f bin -o $@ $<

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

test:
	$(PYTHON) -m pytest -q

clean:
	./dosctl stop --all 2>/dev/null || true
	rm -rf build
