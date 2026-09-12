import { Injectable } from '@angular/core';
import { signal } from '@angular/core';

export type ConnectionStatus = 'desconectado' | 'conectando' | 'conectado';

@Injectable({ providedIn: 'root' })
export class RobotSocketService {

  readonly status = signal<ConnectionStatus>('desconectado');
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

  send(message: string): boolean {
    if (this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(message);
      return true;
    }
    return false;
  }

  disconnect() {
    if (this.socket !== null) {
      this.socket.close();
      this.socket = null;
    }
  }
}
