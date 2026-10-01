#!/usr/bin/env python3

import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOGIC = ROOT / "Servidor" / "build-sim" / "logica_simulador"
LIBRARY = ROOT / "Servidor" / "build-sim" / "roombateca"
STATE = ROOT / "Logica" / "estado.json"
SOCKET_TIMEOUT = 2.0


def read_state(reader):
    line = reader.readline()
    if not line:
        raise RuntimeError("La logica cerro el socket")
    return json.loads(line)


def send_request(client, reader, request):
    client.sendall((json.dumps(request, separators=(",", ":")) + "\n").encode())
    return read_state(reader)


def wait_for_pose(reader, minimum_x):
    deadline = time.monotonic() + SOCKET_TIMEOUT
    while time.monotonic() < deadline:
        state = read_state(reader)
        pose = state["reported"].get("pose", {})
        if pose.get("xMm", 0) >= minimum_x:
            return state
    raise AssertionError(f"La pose no alcanzo xMm >= {minimum_x}")


def read_state_with_pose(reader):
    deadline = time.monotonic() + SOCKET_TIMEOUT
    while time.monotonic() < deadline:
        state = read_state(reader)
        if "pose" in state["reported"]:
            return state
    raise AssertionError("La logica no publico reported.pose")


def main():
    if not LOGIC.exists():
        raise SystemExit(
            "Falta el binario. Ejecuta: "
            "cmake --build Servidor/build-sim --target logica_simulador"
        )

    with tempfile.TemporaryDirectory(prefix="roomba-mapa-") as directory:
        directory = Path(directory)
        state_path = directory / "estado.json"
        socket_path = directory / "logica.sock"
        shutil.copy(STATE, state_path)
        logic = subprocess.Popen(
            [str(LOGIC), str(state_path), str(socket_path)],
            cwd=ROOT,
            env={"LD_LIBRARY_PATH": str(LIBRARY), **__import__("os").environ},
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            deadline = time.monotonic() + SOCKET_TIMEOUT
            while not socket_path.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            if not socket_path.exists():
                raise RuntimeError(logic.stderr.read())

            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.settimeout(SOCKET_TIMEOUT)
            client.connect(str(socket_path))
            reader = client.makefile("r")
            initial = read_state_with_pose(reader)
            assert len(initial["reported"]["sensors"]) == 2
            assert initial["reported"]["map"]["width"] == 8
            assert initial["reported"]["map"]["height"] == 6
            assert initial["reported"]["pose"]["xMm"] == 0

            send_request(client, reader, {
                "type": "set_state",
                "desired": {"motion": {"direction": "FWD", "speed": 500}},
            })
            moving = wait_for_pose(reader, 100)
            assert moving["reported"]["pose"]["xMm"] >= 100
            assert sum(cell == 1 for cell in moving["reported"]["map"]["cells"]) > 0

            stopped = send_request(client, reader, {
                "type": "set_state",
                "desired": {"motion": {"direction": "STOP", "speed": 0}},
            })
            stopped_x = stopped["reported"]["pose"]["xMm"]
            time.sleep(0.25)
            after_stop = send_request(client, reader, {"type": "get_state"})
            assert abs(after_stop["reported"]["pose"]["xMm"] - stopped_x) < 20

            client.close()
            print("OK: movimiento -> pose -> mapa -> STOP")
            print("Pose final:", after_stop["reported"]["pose"])
            print("Celdas visitadas:", sum(cell == 1 for cell in after_stop["reported"]["map"]["cells"]))
        finally:
            logic.terminate()
            logic.wait(timeout=SOCKET_TIMEOUT)


if __name__ == "__main__":
    main()
