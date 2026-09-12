import { Component, Input, Output, EventEmitter } from '@angular/core';
import {MatButtonModule} from '@angular/material/button';
import {MatCardModule} from '@angular/material/card';

import { MatSliderModule } from '@angular/material/slider';
import { MatSlideToggle } from '@angular/material/slide-toggle';



@Component({
  selector: 'mode-card',
  imports: [
    MatButtonModule,
    MatCardModule,
    MatSlideToggle,
    MatSliderModule],
  templateUrl: './loop.component.html',
  styleUrl: './loop.component.scss'
})
export class LoopComponent {


  @Input() titulo: string = '';
  @Input() descripcion: string = '';

  @Output() enableChange = new EventEmitter<boolean>();
  @Output() valueChange = new EventEmitter<number>();

  enableMode: boolean = false;
  disabledLoopMode: boolean = !this.enableMode;

  max = 1000;
  min = 0;
  showTicks = false;
  step = 1;
  thumbLabel = true;
  value = 0;

  percent: number = 0;

  formatLabel(value: number) {
    this.percent = value / (this.max - this.min) * 100;
    if (this.percent == 100) {
      return 'Max';
    } else if (value == 0) {
      return 'Off';
    }
    return this.percent.toFixed(0) + '%';
  }


  onEnableLoopChange(event: any) {
    this.enableMode = event.checked;
    this.disabledLoopMode = !this.enableMode;
    this.enableChange.emit(this.enableMode);
    console.log('Habilitar modo:', this.enableMode);
  }

  onSliderChange(value: number) {
    this.value = value;
    this.valueChange.emit(this.value);
    this.percent = value / (this.max - this.min) * 100;
    console.log('Valor del slider:', value);
    console.log('Porcentaje del slider:', this.percent);
  }

}
