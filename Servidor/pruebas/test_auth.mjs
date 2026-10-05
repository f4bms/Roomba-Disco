#!/usr/bin/env node
// Prueba de integracion del login: valida el handshake reto-respuesta contra el
// servidor C real. El SHA-256 de Node cruza con la implementacion propia en C.

import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { randomBytes, createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { createConnection } from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const build = join(root, 'Servidor/build');
const temporary = mkdtempSync(join(tmpdir(), 'roomba-auth-'));
const socketPath = join(temporary, 'logica.sock');
const usersFile = join(temporary, 'usuarios.conf');
const webRoot = join(temporary, 'www');
const statePath = join(temporary, 'estado.json');
const port = 19190;
const PASSWORD = 'roomba123';

mkdirSync(webRoot);
writeFileSync(statePath, execFileSync('cat', [join(root, 'Logica/estado.json')]));
execFileSync(join(build, 'crear_usuario'), ['admin', PASSWORD, usersFile]);

function sha256(buffer) {
  return createHash('sha256').update(buffer).digest();
}

function solve(password, saltHex, challengeHex) {
  const verifier = sha256(Buffer.concat([Buffer.from(saltHex, 'hex'), Buffer.from(password, 'utf8')]));
  return sha256(Buffer.concat([verifier, Buffer.from(challengeHex, 'hex')])).toString('hex');
}

function websocket() {
  return new Promise((resolveConnection, reject) => {
    const connection = createConnection(port, '127.0.0.1');
    const key = randomBytes(16).toString('base64');
    let buffer = Buffer.alloc(0);
    const queue = [];
    const waiters = [];
    const deliver = message => {
      const waiter = waiters.shift();
      if (waiter) waiter(message);
      else queue.push(message);
    };
    connection.once('error', reject);
    connection.on('connect', () => {
      connection.write(`GET /ws HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\n` +
        `Connection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`);
    });
    let ready = false;
    connection.on('data', chunk => {
      buffer = Buffer.concat([buffer, chunk]);
      if (!ready) {
        const end = buffer.indexOf('\r\n\r\n');
        if (end < 0) return;
        assert.match(buffer.subarray(0, end).toString(), /101 Switching Protocols/);
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
          next() {
            return queue.length ? Promise.resolve(queue.shift())
              : new Promise(res => waiters.push(res));
          },
          close() { connection.destroy(); },
        });
      }
      while (buffer.length >= 2) {
        let headerLength = 2;
        let length = buffer[1] & 127;
        if (length === 126) { if (buffer.length < 4) return; length = buffer.readUInt16BE(2); headerLength = 4; }
        else if (length === 127) { if (buffer.length < 10) return; length = Number(buffer.readBigUInt64BE(2)); headerLength = 10; }
        if (buffer.length < headerLength + length) return;
        deliver(JSON.parse(buffer.subarray(headerLength, headerLength + length).toString()));
        buffer = buffer.subarray(headerLength + length);
      }
    });
  });
}

async function nextOfType(client, type, tries = 100) {
  for (let i = 0; i < tries; i++) {
    const message = await client.next();
    if (message.type === type) return message;
  }
  throw new Error(`no llego mensaje ${type}`);
}

const logic = spawn(join(build, 'logica_simulador'), [statePath, socketPath],
  { cwd: root, stdio: 'ignore' });
const server = spawn(join(build, 'servidor'), [String(port), webRoot, socketPath, usersFile],
  { cwd: root, stdio: 'ignore' });

async function waitForServer() {
  for (let attempt = 0; attempt < 60; attempt++) {
    try { return await websocket(); } catch { await delay(100); }
  }
  throw new Error('servidor no disponible');
}

try {
  // 1) Contrasena incorrecta: rechazo y sin acceso al control.
  const wrong = await waitForServer();
  wrong.send({ type: 'auth_init', user: 'admin' });
  const challengeWrong = await nextOfType(wrong, 'auth_challenge');
  wrong.send({ type: 'auth_response', response: solve('incorrecta', challengeWrong.salt, challengeWrong.challenge) });
  const resultWrong = await nextOfType(wrong, 'auth_result');
  assert.equal(resultWrong.ok, false);
  wrong.send({ type: 'get_state' });
  const blocked = await nextOfType(wrong, 'error');
  assert.match(blocked.message, /no autenticado/);
  wrong.close();

  // 2) Usuario inexistente: tambien rechazado.
  const ghost = await websocket();
  ghost.send({ type: 'auth_init', user: 'fantasma' });
  const challengeGhost = await nextOfType(ghost, 'auth_challenge');
  ghost.send({ type: 'auth_response', response: solve(PASSWORD, challengeGhost.salt, challengeGhost.challenge) });
  assert.equal((await nextOfType(ghost, 'auth_result')).ok, false);
  ghost.close();

  // 3) Credenciales correctas: acceso y estado.
  const ok = await websocket();
  ok.send({ type: 'auth_init', user: 'admin' });
  const challenge = await nextOfType(ok, 'auth_challenge');
  ok.send({ type: 'auth_response', response: solve(PASSWORD, challenge.salt, challenge.challenge) });
  assert.equal((await nextOfType(ok, 'auth_result')).ok, true);
  ok.send({ type: 'get_state' });
  const state = await nextOfType(ok, 'state');
  assert.ok(Array.isArray(state.reported.sensors));
  ok.close();

  console.log('OK: login reto-respuesta (rechazo, usuario inexistente y acceso con estado)');
} finally {
  server.kill();
  logic.kill();
  rmSync(temporary, { recursive: true, force: true });
}
