#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/Servidor/build"
USERS_FILE="$ROOT/Servidor/usuarios.conf"
LOG_DIR="$(mktemp -d "${TMPDIR:-/tmp}/roomba-demo-logs.XXXXXX")"
STATE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/roomba-demo-state.XXXXXX")"
LOGIC_SOCKET="$STATE_DIR/logica.sock"
LOGIC_PID=""
SERVER_PID=""
CLIENT_PID=""
TAIL_PID=""

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  for pid in "$TAIL_PID" "$CLIENT_PID" "$SERVER_PID" "$LOGIC_PID"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  for pid in "$TAIL_PID" "$CLIENT_PID" "$SERVER_PID" "$LOGIC_PID"; do
    if [[ -n "$pid" ]]; then wait "$pid" 2>/dev/null || true; fi
  done
  rm -rf "$STATE_DIR"
  printf '\nDemo detenida. Logs conservados en: %s\n' "$LOG_DIR"
  exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

port_is_open() {
  (echo >/dev/tcp/127.0.0.1/"$1") >/dev/null 2>&1
}

wait_for_port() {
  local port=$1
  local pid=$2
  local label=$3
  for _ in {1..100}; do
    if ! kill -0 "$pid" 2>/dev/null; then
      printf '%s terminó durante el arranque. Revisa %s\n' "$label" "$LOG_DIR/$label.log" >&2
      return 1
    fi
    if port_is_open "$port"; then return 0; fi
    sleep 0.1
  done
  printf 'Timeout esperando %s en el puerto %s. Revisa %s\n' "$label" "$port" "$LOG_DIR/$label.log" >&2
  return 1
}

for command in cmake npm node; do
  if ! command -v "$command" >/dev/null 2>&1; then
    printf 'Falta el comando requerido: %s\n' "$command" >&2
    exit 1
  fi
done

if [[ ! -f "$USERS_FILE" ]]; then
  printf 'No existe el almacén local de usuarios: %s\n' "$USERS_FILE" >&2
  printf 'Créalo primero con: ./Servidor/build/crear_usuario <usuario> <clave> Servidor/usuarios.conf\n' >&2
  exit 1
fi

for port in 8080 4200; do
  if port_is_open "$port"; then
    printf 'El puerto %s ya está ocupado. Detén el proceso que lo usa y vuelve a ejecutar este script.\n' "$port" >&2
    exit 1
  fi
done

cp "$ROOT/Logica/estado.json" "$STATE_DIR/estado.json"

printf 'Configurando backend con driver simulado...\n'
cmake -S "$ROOT/Servidor" -B "$BUILD_DIR" \
  -DROOMBATECA_HARDWARE=OFF \
  -DBUILD_CLIENT=OFF
cmake --build "$BUILD_DIR" --target servidor logica_simulador crear_usuario

printf 'Iniciando Lógica simulada...\n'
"$BUILD_DIR/logica_simulador" "$STATE_DIR/estado.json" "$LOGIC_SOCKET" \
  >"$LOG_DIR/logica.log" 2>&1 &
LOGIC_PID=$!
for _ in {1..100}; do
  if [[ -S "$LOGIC_SOCKET" ]]; then break; fi
  if ! kill -0 "$LOGIC_PID" 2>/dev/null; then
    printf 'Lógica terminó durante el arranque. Revisa %s\n' "$LOG_DIR/logica.log" >&2
    exit 1
  fi
  sleep 0.1
done
if [[ ! -S "$LOGIC_SOCKET" ]]; then
  printf 'Lógica no creó el socket IPC. Revisa %s\n' "$LOG_DIR/logica.log" >&2
  exit 1
fi

printf 'Iniciando servidor con el almacén local de usuarios...\n'
"$BUILD_DIR/servidor" 8080 "" "$LOGIC_SOCKET" "$USERS_FILE" \
  >"$LOG_DIR/servidor.log" 2>&1 &
SERVER_PID=$!
wait_for_port 8080 "$SERVER_PID" servidor

if [[ ! -x "$ROOT/Cliente/node_modules/.bin/ng" ]]; then
  printf 'Instalando dependencias Angular (solo es necesario la primera vez)...\n'
  npm --prefix "$ROOT/Cliente" ci
fi

printf 'Iniciando cliente Angular...\n'
"$ROOT/Cliente/node_modules/.bin/ng" serve --host 127.0.0.1 --port 4200 \
  >"$LOG_DIR/cliente.log" 2>&1 &
CLIENT_PID=$!
wait_for_port 4200 "$CLIENT_PID" cliente

printf '\nSistema listo: http://127.0.0.1:4200/login\n'
printf 'En el formulario usa 127.0.0.1:8080 y una cuenta guardada en Servidor/usuarios.conf.\n'
printf 'La contraseña de prueba compartida que configuraste es débil; úsala solo localmente.\n'
printf 'Pulsa Ctrl+C para detener los tres procesos.\n'
printf 'Logs: %s\n\n' "$LOG_DIR"

tail -n 0 -F "$LOG_DIR/servidor.log" "$LOG_DIR/cliente.log" &
TAIL_PID=$!
wait "$TAIL_PID"
