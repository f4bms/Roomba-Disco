import { Component } from '@angular/core';
import { JoystickComponent } from "./joystick/joystick.component";
import { MatCardModule } from '@angular/material/card';
import { MatSliderModule } from '@angular/material/slider';
import { MatSlideToggle } from '@angular/material/slide-toggle';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'component-controller',
  imports: [
    MatSlideToggle,
    FormsModule,
    JoystickComponent,
    MatCardModule,
    MatSliderModule,
  ],
  templateUrl: './controller.component.html',
  styleUrl: './controller.component.scss'
})


export class ControllerComponent {

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


