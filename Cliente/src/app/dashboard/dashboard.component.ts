import { Component } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatSliderModule } from '@angular/material/slider';
import { JoystickComponent } from '../controller/joystick/joystick.component';
import { AudioPanelComponent } from './audio-panel/audio-panel.component';
import { MapViewComponent } from './map-view/map-view.component';

interface SensorReading {
  id: number;
  distance: number;
  obstacle: boolean;
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
export class DashboardComponent {

  powered = true;
  mode: 'AUTO' | 'MANUAL' = 'MANUAL';
  speed = 0;
  angle: number | null = null;
  direction: string | null = null;

  sensors: SensorReading[] = [
    { id: 1, distance: 120, obstacle: false },
    { id: 2, distance: 45, obstacle: true },
    { id: 3, distance: 200, obstacle: false },
  ];

  toggleMode() {
    this.mode = this.mode === 'MANUAL' ? 'AUTO' : 'MANUAL';
  }

  onAngleChange(angle: number) {
    this.angle = angle;
  }

  onDirectionChange(direction: string) {
    this.direction = direction;
  }

  onSpeedChange(value: number) {
    this.speed = value;
  }
}
