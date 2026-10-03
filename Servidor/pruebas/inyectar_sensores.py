#!/usr/bin/env python3

import argparse
import math
import os
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Modificar los sensores simulados de Lógica sin reiniciarla")
    parser.add_argument("frontal", type=float, help="distancia frontal en cm")
    parser.add_argument("trasero", type=float, help="distancia trasera en cm")
    parser.add_argument("--file", type=Path, default=Path("/tmp/roomba-sensores.txt"))
    args = parser.parse_args()
    distances = (args.frontal, args.trasero)
    if any(not math.isfinite(value) or not 2 <= value <= 400 for value in distances):
        parser.error("las distancias deben estar entre 2 y 400 cm")

    path = args.file.resolve()
    descriptor, temporary_path = tempfile.mkstemp(prefix=f".{path.name}-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii") as output:
            output.write(" ".join(str(value) for value in distances) + "\n")
        os.replace(temporary_path, path)
    finally:
        if os.path.exists(temporary_path):
            os.unlink(temporary_path)
    print(f"Sensores frontal/trasero: {distances} cm -> {path}")


if __name__ == "__main__":
    main()