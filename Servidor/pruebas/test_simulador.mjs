import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { randomBytes, createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { createConnection } from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const temporary = mkdtempSync(join(tmpdir(), 'roomba-sim-'));
const socketPath = join(temporary, 'logic.sock');
const webRoot = join(temporary, 'www');
const port = 19080;
mkdirSync(webRoot);
const logic = spawn('node', [join(root, 'Servidor/pruebas/simulador_estado.mjs'),
  join(root, 'Logica/estado.json'), socketPath], { cwd: root, stdio: ['pipe', 'pipe', 'pipe'] });
const server = spawn(join(root, 'Servidor/build/servidor'), [String(port), webRoot, socketPath],
  { cwd: root, stdio: 'ignore' });

function websocket() {
  return new Promise((resolveConnection, reject) => {
    const connection = createConnection(port, '127.0.0.1');
    const key = randomBytes(16).toString('base64');
    let buffer = Buffer.alloc(0);
    const queue = [];
    const pending = [];
    const next = message => {
      const consumer = pending.shift();
      if (consumer) consumer(message);
      else queue.push(message);
    };
    connection.once('error', reject);
    connection.on('connect', () => {
      connection.write(`GET /ws HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`);
    });
    let ready = false;
    connection.on('data', chunk => {
      buffer = Buffer.concat([buffer, chunk]);
      if (!ready) {
        const end = buffer.indexOf('\r\n\r\n');
        if (end < 0) return;
        const response = buffer.subarray(0, end).toString();
        const accept = createHash('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
        assert.match(response, /101 Switching Protocols/);
        assert.ok(response.toLowerCase().includes(`sec-websocket-accept: ${accept.toLowerCase()}`));
        buffer = buffer.subarray(end + 4);
        ready = true;
        resolveConnection({
          send(message) {
            const payload = Buffer.from(JSON.stringify(message));
            const mask = randomBytes(4);
            const header = payload.length < 126
              ? Buffer.from([0x81, 0x80 | payload.length])
              : Buffer.from([0x81, 0xfe, payload.length >> 8, payload.length & 255]);
            connection.write(Buffer.concat([header, mask,
              Buffer.from(payload.map((byte, index) => byte ^ mask[index % 4]))]));
          },
          async until(predicate) {
            for (let index = 0; index < 100; index++) {
              const message = queue.length ? queue.shift() : await new Promise(done => pending.push(done));
              if (predicate(message)) return message;
            }
            throw new Error('No llegó el estado esperado');
          },
          close() { connection.destroy(); },
        });
      }
      while (buffer.length >= 2) {
        let headerLength = 2;
        let length = buffer[1] & 127;
        if (length === 126) {
          if (buffer.length < 4) return;
          length = buffer.readUInt16BE(2);
          headerLength = 4;
        } else if (length === 127) {
          if (buffer.length < 10) return;
          length = Number(buffer.readBigUInt64BE(2));
          headerLength = 10;
        }
        if (buffer.length < headerLength + length) return;
        next(JSON.parse(buffer.subarray(headerLength, headerLength + length).toString()));
        buffer = buffer.subarray(headerLength + length);
      }
    });
  });
}

async function waitForServer() {
  for (let attempt = 0; attempt < 60; attempt++) {
    try { return await websocket(); } catch { await delay(100); }
  }
  throw new Error('Servidor no disponible');
}

let first;
let second;
try {
  first = await waitForServer();
  second = await websocket();
  first.send({ type: 'get_state' });
  const initial = await first.until(message => message.type === 'state');
  first.send({ type: 'set_state', desired: { motion: { direction: 'TURN_L', speed: 32 } } });
  const changed = await first.until(message => message.revision > initial.revision
    && message.desired.motion.direction === 'TURN_L');
  assert.equal(changed.reported.motion.speed, 32);
  const mirrored = await second.until(message => message.revision === changed.revision);
  assert.equal(mirrored.reported.motion.direction, 'TURN_L');

  logic.stdin.write(`${JSON.stringify({ reported: { sensors: [
    { id: 1, distanceCm: 7, obstacle: true },
    { id: 2, distanceCm: 42, obstacle: false },
    { id: 3, distanceCm: 42, obstacle: false },
  ] } })}\n`);
  const pushed = await first.until(message => message.revision > changed.revision
    && message.reported.sensors[0].distanceCm === 7);
  assert.equal(pushed.reported.sensors[0].obstacle, true);
  assert.equal((await second.until(message => message.revision === pushed.revision))
    .reported.sensors[0].distanceCm, 7);
  console.log('OK: cliente -> servidor -> simulador y simulador -> servidor -> dos clientes');
} finally {
  first?.close();
  second?.close();
  server.kill();
  logic.kill();
  rmSync(temporary, { recursive: true, force: true });
}