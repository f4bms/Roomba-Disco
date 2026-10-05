import { Injectable } from '@angular/core';
import { signal } from '@angular/core';
import { solveAuthChallenge } from './sha256';

export type ConnectionStatus = 'desconectado' | 'conectando' | 'conectado';
export type AuthStatus = 'anonimo' | 'autenticando' | 'autenticado' | 'rechazado';

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
    modeIndicator?: 'AUTO' | 'MANUAL' | 'OFF';
    alertIndicator?: boolean;
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
  readonly authState = signal<AuthStatus>('anonimo');
  readonly state = signal<RobotState | null>(null);
  readonly error = signal<string | null>(null);
  readonly serverAddress = signal<string>(this.loadServerAddress());
  private socket: WebSocket | null = null;
  private shouldReconnect = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private openResolvers: Array<{ resolve: () => void; reject: () => void }> = [];
  private pendingLogin: ((ok: boolean) => void) | null = null;
  private authUser = '';
  private authPassword = '';

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
        const resolvers = this.openResolvers;
        this.openResolvers = [];
        resolvers.forEach(resolver => resolver.resolve());
      }
    };
    socket.onmessage = event => {
      let message: { type?: unknown; salt?: unknown; challenge?: unknown; ok?: unknown };
      try {
        message = JSON.parse(String(event.data));
      } catch {
        this.error.set('El servidor envio un JSON invalido');
        return;
      }
      if (message?.type === 'auth_challenge'
          && typeof message.salt === 'string' && typeof message.challenge === 'string') {
        const response = solveAuthChallenge(this.authPassword, message.salt, message.challenge);
        this.sendJson({ type: 'auth_response', response });
        return;
      }
      if (message?.type === 'auth_result') {
        const ok = message.ok === true;
        this.authPassword = '';
        this.authState.set(ok ? 'autenticado' : 'rechazado');
        if (ok) {
          this.error.set(null);
          this.sendJson({ type: 'get_state' });
        }
        this.resolvePendingLogin(ok);
        return;
      }
      if (this.isRobotState(message)) {
        this.state.set(message);
        this.error.set(null);
      } else if (this.isErrorMessage(message)) {
        this.error.set(message.message);
      }
    };
    socket.onclose = () => {
      if (this.socket === socket) {
        this.status.set('desconectado');
        this.socket = null;
        this.authState.set('anonimo');
        this.state.set(null);
        this.rejectPendingOpen();
        this.resolvePendingLogin(false);
        this.scheduleReconnect();
      }
    };
    socket.onerror = () => {
      if (this.socket === socket) {
        this.status.set('desconectado');
        this.rejectPendingOpen();
      }
    };
  }

  async login(user: string, password: string): Promise<boolean> {
    this.authUser = user;
    this.authPassword = password;
    this.authState.set('autenticando');
    try {
      await this.ensureOpen();
    } catch {
      this.authPassword = '';
      this.authState.set('rechazado');
      return false;
    }
    this.sendJson({ type: 'auth_init', user });
    return new Promise<boolean>(resolve => { this.pendingLogin = resolve; });
  }

  logout() {
    this.authState.set('anonimo');
    this.state.set(null);
    this.disconnect();
  }

  sendDesired(desired: DesiredStatePatch): boolean {
    if (this.authState() !== 'autenticado') return false;
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
    this.rejectPendingOpen();
    this.resolvePendingLogin(false);
  }

  private ensureOpen(): Promise<void> {
    if (this.socket?.readyState === WebSocket.OPEN) return Promise.resolve();
    return new Promise<void>((resolve, reject) => {
      this.openResolvers.push({ resolve, reject });
      this.connect();
    });
  }

  private rejectPendingOpen() {
    const resolvers = this.openResolvers;
    this.openResolvers = [];
    resolvers.forEach(resolver => resolver.reject());
  }

  private resolvePendingLogin(ok: boolean) {
    const resolve = this.pendingLogin;
    this.pendingLogin = null;
    if (resolve) resolve(ok);
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
