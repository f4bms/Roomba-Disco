#!/usr/bin/env python3

import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "Servidor" / "build"
WEB_ROOT = ROOT / "Cliente" / "dist" / "scrap-e-controller" / "browser"
PORT = 18080


def wait_for_port(port: int, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"El servidor no abrio el puerto {port}")


def websocket_connect(port: int) -> socket.socket:
    connection = socket.create_connection(("127.0.0.1", port), timeout=3)
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        "GET /ws HTTP/1.1\r\n"
        f"Host: 127.0.0.1:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    connection.sendall(request.encode())
    response = b""
    while b"\r\n\r\n" not in response:
        response += connection.recv(4096)
    expected = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
    ).decode()
    assert b"101 Switching Protocols" in response
    assert f"Sec-WebSocket-Accept: {expected}".encode().lower() in response.lower()
    return connection


def send_text(connection: socket.socket, payload: dict) -> None:
    data = json.dumps(payload, separators=(",", ":")).encode()
    mask = os.urandom(4)
    header = bytearray([0x81])
    if len(data) < 126:
        header.append(0x80 | len(data))
    elif len(data) <= 65535:
        header.append(0x80 | 126)
        header.extend(struct.pack("!H", len(data)))
    else:
        header.append(0x80 | 127)
        header.extend(struct.pack("!Q", len(data)))
    header.extend(mask)
    header.extend(byte ^ mask[index % 4] for index, byte in enumerate(data))
    connection.sendall(header)


def receive_text(connection: socket.socket) -> dict:
    first, second = connection.recv(2)
    assert first & 0x0F == 1
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", connection.recv(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", connection.recv(8))[0]
    payload = bytearray()
    while len(payload) < length:
        payload.extend(connection.recv(length - len(payload)))
    return json.loads(payload.decode())


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        temporary = Path(temporary_directory)
        state_path = temporary / "estado.json"
        socket_path = temporary / "logica.sock"
        shutil.copy(ROOT / "Logica" / "estado.json", state_path)

        logic = subprocess.Popen(
            [str(BUILD / "logica_simulador"), str(state_path), str(socket_path)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        server = subprocess.Popen(
            [str(BUILD / "servidor"), str(PORT), str(WEB_ROOT), str(socket_path)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            wait_for_port(PORT)
            connection = websocket_connect(PORT)
            send_text(connection, {"type": "get_state"})
            initial = receive_text(connection)
            assert initial["type"] == "state"
            assert initial["reported"]["sensors"][1]["distanceCm"] == 18.2

            send_text(connection, {
                "type": "set_state",
                "desired": {
                    "mode": "AUTO",
                    "motion": {"direction": "FWD", "speed": 321},
                    "audio": {"action": "PLAY", "volume": 73},
                },
            })
            updated = receive_text(connection)
            assert updated["revision"] == initial["revision"] + 1
            assert updated["desired"]["motion"] == {"direction": "FWD", "speed": 321}
            assert updated["reported"]["mode"] == "AUTO"
            assert updated["reported"]["audio"]["status"] == "playing"
            assert updated["reported"]["audio"]["volume"] == 73

            persisted = json.loads(state_path.read_text())
            assert persisted == updated
            connection.close()
            print("OK: navegador WebSocket -> servidor -> Logica C -> JSON -> navegador")
        finally:
            server.terminate()
            logic.terminate()
            server.wait(timeout=5)
            logic.wait(timeout=5)


if __name__ == "__main__":
    main()
