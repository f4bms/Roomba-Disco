#!/usr/bin/env python3

import argparse
import base64
import hashlib
import json
import os
import select
import socket
import struct
import sys
from urllib.parse import urlsplit


def connect(url):
    parsed = urlsplit(url)
    if parsed.scheme != "ws" or not parsed.hostname or parsed.path not in ("", "/ws"):
        raise ValueError("Usa ws://HOST:PUERTO/ws (el servidor actual no ofrece WSS)")
    host = parsed.hostname
    port = parsed.port or 80
    connection = socket.create_connection((host, port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET /ws HTTP/1.1\r\nHost: {host}:{port}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    )
    connection.sendall(request.encode("ascii"))
    response = bytearray()
    while b"\r\n\r\n" not in response:
        chunk = connection.recv(4096)
        if not chunk or len(response) > 8192:
            raise ConnectionError("Handshake WebSocket incompleto")
        response.extend(chunk)
    end = response.index(b"\r\n\r\n") + 4
    headers = response[:end].decode("latin-1").lower()
    expected = base64.b64encode(hashlib.sha1(
        (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
    ).digest()).decode("ascii").lower()
    if " 101 " not in headers.split("\r\n", 1)[0] or f"sec-websocket-accept: {expected}" not in headers:
        connection.close()
        raise ConnectionError("El servidor rechazó el handshake WebSocket")
    connection.settimeout(None)
    return connection, response[end:]


def send_frame(connection, opcode, payload):
    mask = os.urandom(4)
    length = len(payload)
    if length < 126:
        header = struct.pack("!BB", 0x80 | opcode, 0x80 | length)
    elif length <= 65535:
        header = struct.pack("!BBH", 0x80 | opcode, 0x80 | 126, length)
    else:
        header = struct.pack("!BBQ", 0x80 | opcode, 0x80 | 127, length)
    connection.sendall(header + mask + bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload)))


def send_json(connection, message):
    send_frame(connection, 1, json.dumps(message, separators=(",", ":")).encode("utf-8"))


def read_frames(buffer):
    messages = []
    offset = 0
    while len(buffer) - offset >= 2:
        first, second = buffer[offset:offset + 2]
        length = second & 127
        header = 2
        if length == 126:
            if len(buffer) - offset < 4:
                break
            length = struct.unpack_from("!H", buffer, offset + 2)[0]
            header = 4
        elif length == 127:
            if len(buffer) - offset < 10:
                break
            length = struct.unpack_from("!Q", buffer, offset + 2)[0]
            header = 10
        if length > 256 * 1024:
            raise ValueError("Frame mayor al límite de 256 KiB")
        if len(buffer) - offset < header + length:
            break
        payload = bytes(buffer[offset + header:offset + header + length])
        if second & 0x80:
            raise ValueError("Un servidor no debe enviar frames enmascarados")
        messages.append((first & 15, payload))
        offset += header + length
    return messages, buffer[offset:]


def describe(state):
    reported = state.get("reported", {})
    motion = reported.get("motion", {})
    sensors = reported.get("sensors", [])
    pose = reported.get("pose", {})
    audio = reported.get("audio", {})
    return (
        f"revision={state.get('revision')} modo={reported.get('mode')} "
        f"motor={motion.get('direction')}:{motion.get('speed')} "
        f"sensores={[(item.get('id'), item.get('distanceCm')) for item in sensors]} "
        f"pose={pose} audio={audio.get('status')}:{audio.get('volume')}"
    )


def input_message(line):
    if line == "get":
        return {"type": "get_state"}
    patch = json.loads(line)
    if not isinstance(patch, dict):
        raise ValueError("Escribe un objeto JSON")
    if patch.get("type") == "set_state":
        patch = patch.get("desired")
    if not isinstance(patch, dict) or not patch or set(patch) - {"mode", "motion", "audio"}:
        raise ValueError("Solo se pueden enviar parches desired: mode, motion o audio")
    return {"type": "set_state", "desired": patch}


def main():
    parser = argparse.ArgumentParser(description="Consola para la Lógica real vía servidor WebSocket")
    parser.add_argument("--url", default="ws://127.0.0.1:8080/ws")
    parser.add_argument("--full", action="store_true", help="Mostrar snapshots completos, incluso repetidos")
    args = parser.parse_args()
    connection, buffer = connect(args.url)
    print(f"Conectado a {args.url}. Comandos: get, JSON desired, Ctrl+C para salir.", flush=True)
    print('Ejemplo: {"motion":{"direction":"FWD","speed":200}}', flush=True)
    send_json(connection, {"type": "get_state"})
    previous = None
    try:
        while True:
            readable, _, _ = select.select([connection, sys.stdin], [], [], 1)
            if connection in readable:
                chunk = connection.recv(65536)
                if not chunk:
                    raise ConnectionError("El servidor cerró el WebSocket")
                buffer += chunk
            frames, buffer = read_frames(buffer)
            for opcode, payload in frames:
                if opcode == 8:
                    return
                if opcode == 9:
                    send_frame(connection, 10, payload)
                if opcode != 1:
                    continue
                message = json.loads(payload)
                if args.full or message != previous:
                    print(json.dumps(message, ensure_ascii=False) if args.full else describe(message), flush=True)
                previous = message
            if sys.stdin in readable:
                line = sys.stdin.readline()
                if not line:
                    return
                try:
                    send_json(connection, input_message(line.strip()))
                except (ValueError, json.JSONDecodeError) as error:
                    print(f"Entrada inválida: {error}", file=sys.stderr, flush=True)
    finally:
        connection.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ConnectionError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)