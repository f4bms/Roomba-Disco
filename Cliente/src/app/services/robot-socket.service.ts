import { Injectable } from '@angular/core';
import { signal } from '@angular/core';

export type ConnectionStatus = 'desconectado' | 'conectando' | 'conectado';

export interface RobotState {
  type: 'state';
  protocolVersion: number;
  revision: number;
  desired: {
    mode: 'AUTO' | 'MANUAL';
    motion: { direction: string; speed: number };
    audio: { action: string; volume: number };
  };
  reported: {
    power: boolean;
    mode: 'AUTO' | 'MANUAL';
    motion: { direction: string; speed: number };
    audio: { status: 'playing' | 'paused' | 'stopped'; volume: number; track: number; tracks: string[] };
    sensors: Array<{ id: number; distanceCm: number; obstacle: boolean }>;
    map: { width: number; height: number; cells: number[] };
  };
}

export interface DesiredStatePatch {
  mode?: 'AUTO' | 'MANUAL';
  motion?: { direction?: string; speed?: number };
  audio?: { action?: string; volume?: number };
}

@Injectable({ providedIn: 'root' })
export class RobotSocketService {

  readonly status = signal<ConnectionStatus>('desconectado');
  readonly state = signal<RobotState | null>(null);
  readonly error = signal<string | null>(null);
  private socket: WebSocket | null = null;

  connect() {
    if (typeof window === 'undefined') {
      return;
    }
    if (this.socket && (this.socket.readyState === WebSocket.OPEN || this.socket.readyState === WebSocket.CONNECTING)) {
      return;
    }

    const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${scheme}://${window.location.host}/ws`;
    const socket = new WebSocket(url);
    this.socket = socket;
    this.status.set('conectando');

    socket.onopen = () => {
      if (this.socket === socket) {
        this.status.set('conectado');
        this.sendJson({ type: 'get_state' });
      }
    };
    socket.onmessage = event => {
      try {
        const message: unknown = JSON.parse(String(event.data));
        if (this.isRobotState(message)) {
          this.state.set(message);
          this.error.set(null);
        } else if (this.isErrorMessage(message)) {
          this.error.set(message.message);
        }
      } catch {
        this.error.set('El servidor envio un JSON invalido');
      }
    };
    socket.onclose = () => {
      if (this.socket === socket) {
        this.status.set('desconectado');
        this.socket = null;
      }
    };
    socket.onerror = () => {
      if (this.socket === socket) {
        this.status.set('desconectado');
      }
    };
  }

  sendDesired(desired: DesiredStatePatch): boolean {
    return this.sendJson({ type: 'set_state', desired });
  }

  private sendJson(message: object): boolean {
    if (this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify(message));
      return true;
    }
    return false;
  }

  private isRobotState(value: unknown): value is RobotState {
    if (typeof value !== 'object' || value === null) return false;
    const candidate = value as Partial<RobotState>;
    return candidate.type === 'state'
      && candidate.protocolVersion === 1
      && typeof candidate.revision === 'number'
      && typeof candidate.desired === 'object'
      && typeof candidate.reported === 'object';
  }

  private isErrorMessage(value: unknown): value is { type: 'error'; message: string } {
    return typeof value === 'object' && value !== null
      && (value as { type?: unknown }).type === 'error'
      && typeof (value as { message?: unknown }).message === 'string';
  }

  disconnect() {
    if (this.socket !== null) {
      this.socket.close();
      this.socket = null;
    }
  }
}
