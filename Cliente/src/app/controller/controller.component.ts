import { Component, OnDestroy } from '@angular/core';
import { JoystickComponent } from "./joystick/joystick.component";
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatSliderModule } from '@angular/material/slider';
import { MatSlideToggle } from '@angular/material/slide-toggle';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'component-controller',
  imports: [
    MatSlideToggle,
    MatButtonModule,
    FormsModule,
    JoystickComponent,
    MatCardModule,
    MatSliderModule,
  ],
  templateUrl: './controller.component.html',
  styleUrl: './controller.component.scss'
})


export class ControllerComponent implements OnDestroy {

  serverUrl = 'ws://localhost:8080';
  connectionStatus = 'desconectado';
  lastMessage = '';
  private socket: WebSocket | null = null;

  angle: number | null = null;
  direction: string | null = null;
  enable: boolean = false;

  percent: number = 0;


  disabled = !this.enable;
  max = 1000;
  min = 0;
  showTicks = false;
  step = 1;
  thumbLabel = true;
  value = 0;

  connectToServer() {
    this.disconnectFromServer();
    this.connectionStatus = 'conectando';
    this.lastMessage = '';
    const socket = new WebSocket(this.serverUrl);
    this.socket = socket;

    socket.onopen = () => {
      if (this.socket === socket) {
        this.connectionStatus = 'conectado';
      }
    };
    socket.onmessage = (event: MessageEvent) => {
      if (this.socket === socket) {
        this.lastMessage = event.data;
      }
    };
    socket.onerror = () => {
      if (this.socket === socket) {
        this.connectionStatus = 'error';
      }
    };
    socket.onclose = () => {
      if (this.socket === socket) {
        this.connectionStatus = 'desconectado';
        this.socket = null;
      }
    };
  }

  disconnectFromServer() {
    if (this.socket !== null) {
      this.socket.close();
      this.socket = null;
    }
    this.connectionStatus = 'desconectado';
  }

  sendTestMessage() {
    if (this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send('prueba desde Angular');
    }
  }

  ngOnDestroy() {
    this.disconnectFromServer();
  }



  formatLabel(value: number) {
    this.percent = value / (this.max - this.min) * 100;
    if (this.percent == 100) {
      return 'Max';
    } else if (value == 0) {
      return 'Off';
    }
    return this.percent.toFixed(0) + '%';
  }

  onEnableChange(event: any) {
    this.enable = event.checked;
    this.disabled = !this.enable;
    console.log('Habilitar joystick:', this.enable);
  }

  onAngleChange(angle: number) {
  this.angle = angle;
  console.log('Ángulo del joystick:', angle);
  }

  onDirectionChange(direction: string) {
    this.direction = direction;
    console.log('Dirección del joystick:', direction);
  }

  onSliderChange(value: number) {
    this.value = value;
    this.percent = value / (this.max - this.min) * 100;
    console.log('Valor del slider:', value);
    console.log('Porcentaje del slider:', this.percent);
  }
}


