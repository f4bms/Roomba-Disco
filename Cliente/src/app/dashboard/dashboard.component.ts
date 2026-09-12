import { Component, OnDestroy, OnInit, inject } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatSliderModule } from '@angular/material/slider';
import { JoystickComponent } from '../controller/joystick/joystick.component';
import { AudioPanelComponent } from './audio-panel/audio-panel.component';
import { MapViewComponent } from './map-view/map-view.component';
import { RobotSocketService } from '../services/robot-socket.service';

interface SensorReading {
  id: number;
  distance: number | null;
  obstacle: boolean | null;
}

@Component({
  selector: 'app-dashboard',
  imports: [
    MatButtonModule,
    MatCardModule,
    MatSliderModule,
    JoystickComponent,
    AudioPanelComponent,
    MapViewComponent,
  ],
  templateUrl: './dashboard.component.html',
  styleUrl: './dashboard.component.scss',
})
export class DashboardComponent implements OnInit, OnDestroy {

  private readonly socket = inject(RobotSocketService);
  readonly connectionStatus = this.socket.status;

  // Habilitado para prueba aislada cliente-servidor; el valor real vendra de LED:POWER.
  powered = true;
  // Modo local para pruebas; la fuente de verdad sera MODE:STATUS desde Logica.
  mode: 'AUTO' | 'MANUAL' = 'MANUAL';
  speed = 0;

  // Sin datos hasta que Logica reporte SENSOR/LED a traves del servidor.
  sensors: SensorReading[] = [
    { id: 1, distance: null, obstacle: null },
    { id: 2, distance: null, obstacle: null },
    { id: 3, distance: null, obstacle: null },
  ];

  ngOnInit() {
    this.socket.connect();
  }

  ngOnDestroy() {
    this.socket.disconnect();
  }

  toggleMode() {
    this.mode = this.mode === 'MANUAL' ? 'AUTO' : 'MANUAL';
    this.socket.send(`MODE:${this.mode}`);
  }

  onSpeedChange(value: number) {
    this.speed = value;
  }

  onDirectionChange(direction: string) {
    const command = this.directionToCommand(direction);
    if (command !== null) {
      this.socket.send(command);
    }
  }

  private directionToCommand(direction: string): string | null {
    switch (direction) {
      case 'up':
        return `CMD:FWD:${this.speed}`;
      case 'down':
        return `CMD:BACK:${this.speed}`;
      case 'left':
        return 'CMD:TURN_L';
      case 'right':
        return 'CMD:TURN_R';
      case 'standby':
        return 'CMD:STOP';
      default:
        return null;
    }
  }
}
