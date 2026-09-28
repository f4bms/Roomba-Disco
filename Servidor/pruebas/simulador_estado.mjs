import { createServer } from 'node:net';
import { readFileSync, unlinkSync } from 'node:fs';
import { createInterface } from 'node:readline';

const statePath = process.argv[2] ?? 'Logica/estado.json';
const socketPath = process.argv[3] ?? '/tmp/roomba-logica.sock';
const state = JSON.parse(readFileSync(statePath, 'utf8'));
let connection;

function merge(target, patch) {
  for (const [key, value] of Object.entries(patch)) {
    if (value && typeof value === 'object' && !Array.isArray(value)
        && target[key] && typeof target[key] === 'object' && !Array.isArray(target[key])) {
      merge(target[key], value);
    } else {
      target[key] = value;
    }
  }
}

function broadcast() {
  if (connection && !connection.destroyed) {
    connection.write(`${JSON.stringify(state)}\n`);
  }
}

function acceptRequest(request) {
  if (request?.type === 'get_state') {
    broadcast();
    return;
  }
  if (request?.type !== 'set_state' || !request.desired || typeof request.desired !== 'object') {
    return;
  }
  merge(state.desired, request.desired);
  if (request.desired.mode) state.reported.mode = request.desired.mode;
  if (request.desired.motion) merge(state.reported.motion, request.desired.motion);
  if (request.desired.audio) {
    const { action, volume } = request.desired.audio;
    if (volume !== undefined) state.reported.audio.volume = volume;
    if (action === 'PLAY') state.reported.audio.status = 'playing';
    if (action === 'PAUSE') state.reported.audio.status = 'paused';
    if (action === 'STOP') state.reported.audio.status = 'stopped';
    if (action === 'NEXT' || action === 'PREV') {
      const count = state.reported.audio.tracks.length;
      if (count > 0) {
        const step = action === 'NEXT' ? 1 : -1;
        state.reported.audio.track = (state.reported.audio.track + step + count) % count;
      }
    }
  }
  state.revision++;
  console.log(`Cliente -> Lógica simulada: ${JSON.stringify(request.desired)}`);
  broadcast();
}

const server = createServer(socket => {
  if (connection && !connection.destroyed) {
    socket.destroy();
    return;
  }
  connection = socket;
  let buffer = '';
  socket.setEncoding('utf8');
  socket.on('data', data => {
    buffer += data;
    let separator;
    while ((separator = buffer.indexOf('\n')) !== -1) {
      const line = buffer.slice(0, separator);
      buffer = buffer.slice(separator + 1);
      try {
        acceptRequest(JSON.parse(line));
      } catch (error) {
        console.error(`Solicitud inválida: ${error.message}`);
      }
    }
    if (buffer.length > 256 * 1024) socket.destroy();
  });
  socket.on('close', () => {
    if (connection === socket) connection = undefined;
  });
  broadcast();
});

let listening = false;
for (const signal of ['SIGINT', 'SIGTERM']) {
  process.on(signal, () => {
    if (listening) {
      connection?.destroy();
      server.close();
      unlinkSync(socketPath);
      listening = false;
    }
    input.close();
  });
}

server.on('error', error => {
  console.error(`No se pudo escuchar en ${socketPath}: ${error.message}`);
  process.exitCode = 1;
});
server.listen(socketPath, () => {
  listening = true;
  console.log(`Lógica simulada en ${socketPath}. Pega un JSON por línea para actualizar reported.`);
});

const input = createInterface({ input: process.stdin });
input.on('line', line => {
  try {
    const patch = JSON.parse(line);
    if (!patch || typeof patch !== 'object' || Array.isArray(patch)
        || !patch.reported || typeof patch.reported !== 'object' || Array.isArray(patch.reported)) {
      throw new Error('Se requiere un objeto con la clave reported');
    }
    merge(state.reported, patch.reported);
    state.revision++;
    broadcast();
    console.log(`Lógica -> Cliente: revisión ${state.revision}`);
  } catch (error) {
    console.error(`Cambio inválido: ${error.message}`);
  }
});