#!/usr/bin/env python3
"""Boot, then repeat NC collections; on failure dump live PIC/IRQ state."""
import sys, time, socket, threading
sys.path.insert(0, '.')
from harness.dosvm import DosVM

vm = DosVM.start(run_id="ncprobe", timeout=90.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    vm.exec("TYPE NUL>C:\\TMP\\EMPTY.DAT")
    for i in range(6):
        listener = socket.socket()
        listener.bind(("0.0.0.0", 0)); listener.listen(1)
        port = listener.getsockname()[1]
        got = []
        def rx():
            c, _ = listener.accept()
            got.append(c.recv(65536))
            c.close()
        t = threading.Thread(target=rx); t.start()
        try:
            out = vm.exec(f"NC -target 10.0.2.2 {port} -bin < C:\\TMP\\EMPTY.DAT", timeout=40.0)
            print(f"nc {i}: OK ({len(got[0]) if got else 0} bytes)")
        except Exception as e:
            print(f"nc {i}: FAILED — {str(e).splitlines()[0]}")
            print("screen tail:", vm.screen_text().splitlines()[-4:])
            with vm.qmp() as q:
                print(q.hmp("info irq"))
                print(q.hmp("info pic").split("pic1:")[1][:80])
                print(q.hmp("info pic").split("pic0:")[1][:80])
            break
        finally:
            listener.close(); t.join(2.0)
finally:
    vm.stop(force=True)
