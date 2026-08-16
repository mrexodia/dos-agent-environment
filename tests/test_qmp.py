import json
import socket
import threading

import pytest

from harness.qmp import QMPClient, QMPError


def _serve_qmp(path, error=False):
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(path))
    server.listen(1)

    def run():
        connection, _ = server.accept()
        with connection, connection.makefile("rwb", buffering=0) as stream:
            stream.write(json.dumps({"QMP": {"version": {}}}).encode() + b"\r\n")
            capabilities = json.loads(stream.readline())
            stream.write(json.dumps({"return": {}, "id": capabilities["id"]}).encode() + b"\r\n")
            request = json.loads(stream.readline())
            stream.write(json.dumps({"event": "TEST_EVENT", "data": {}}).encode() + b"\r\n")
            if error:
                response = {
                    "error": {"class": "GenericError", "desc": "planned failure"},
                    "id": request["id"],
                }
            else:
                response = {"return": {"status": "running"}, "id": request["id"]}
            stream.write(json.dumps(response).encode() + b"\r\n")
        server.close()

    thread = threading.Thread(target=run)
    thread.start()
    return thread


def test_qmp_handshake_command_and_event(tmp_path):
    path = tmp_path / "qmp.sock"
    thread = _serve_qmp(path)
    with QMPClient(path) as qmp:
        assert qmp.execute("query-status") == {"status": "running"}
        assert qmp.events[0]["event"] == "TEST_EVENT"
    thread.join(2)
    assert not thread.is_alive()


def test_qmp_error_is_reported(tmp_path):
    path = tmp_path / "qmp.sock"
    thread = _serve_qmp(path, error=True)
    with QMPClient(path) as qmp:
        with pytest.raises(QMPError, match="planned failure"):
            qmp.execute("query-status")
    thread.join(2)
