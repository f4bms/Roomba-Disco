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

  private static readonly SERVER_ADDRESS_KEY = 'roomba.serverAddress';

  readonly status = signal<ConnectionStatus>('desconectado');
  readonly state = signal<RobotState | null>(null);
  readonly error = signal<string | null>(null);
  readonly serverAddress = signal<string>(this.loadServerAddress());
  private socket: WebSocket | null = null;
  private shouldReconnect = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  connect() {
    if (typeof window === 'undefined') {
      return;
    }
    this.shouldReconnect = true;
    if (this.socket && (this.socket.readyState === WebSocket.OPEN || this.socket.readyState === WebSocket.CONNECTING)) {
      return;
    }

    const url = this.buildWebSocketUrl();
    if (url === null) {
      return;
    }
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
        this.scheduleReconnect();
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

  setServerAddress(address: string) {
    const trimmed = address.trim();
    this.serverAddress.set(trimmed);
    if (typeof localStorage !== 'undefined') {
      if (trimmed) {
        localStorage.setItem(RobotSocketService.SERVER_ADDRESS_KEY, trimmed);
      } else {
        localStorage.removeItem(RobotSocketService.SERVER_ADDRESS_KEY);
      }
    }
  }

  reconnect() {
    this.shouldReconnect = true;
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.socket !== null) {
      const socket = this.socket;
      this.socket = null;
      socket.close();
    }
    this.connect();
  }

  disconnect() {
    this.shouldReconnect = false;
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.socket !== null) {
      this.socket.close();
      this.socket = null;
    }
  }

  private scheduleReconnect() {
    if (!this.shouldReconnect || this.reconnectTimer !== null) {
      return;
    }
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, 2000);
  }

  private loadServerAddress(): string {
    if (typeof localStorage === 'undefined') {
      return '';
    }
    return localStorage.getItem(RobotSocketService.SERVER_ADDRESS_KEY) ?? '';
  }

  private buildWebSocketUrl(): string | null {
    if (typeof window === 'undefined') {
      return null;
    }
    const configured = this.serverAddress().trim();
    const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
    if (configured === '') {
      return `${scheme}://${window.location.host}/ws`;
    }
    if (configured.startsWith('ws://') || configured.startsWith('wss://')) {
      const base = configured.replace(/\/+$/, '');
      return base.endsWith('/ws') ? base : `${base}/ws`;
    }
    const host = /:\d+$/.test(configured) ? configured : `${configured}:8080`;
    return `${scheme}://${host}/ws`;
  }
}
