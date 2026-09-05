import { Component, Input, Output, EventEmitter, AfterViewInit } from '@angular/core';
import nipplejs from 'nipplejs';

@Component({
  selector: 'controll-movement-joystick',
  templateUrl: './joystick.component.html',
  styleUrls: ['./joystick.component.scss'],
})
export class JoystickComponent implements AfterViewInit {

  @Input() public disabled: boolean = false;

  @Output() public joystickAngle = new EventEmitter<number>();
  @Output() public joystickDirection = new EventEmitter<string>();

  ngAfterViewInit() {
    const zone = document.getElementById('joystick-zone');
    const manager = nipplejs.create({
      zone: zone!,
      mode: 'static',
      position: { left: '50%', top: '50%' },
      color: 'blue',
      follow: false,
      dynamicPage: true
    });

    manager.on('move', (evt, data) => {
      if (!this.disabled) {
        if (data && data.direction) {
          this.joystickAngle.emit(data.angle.degree);
          this.joystickDirection.emit(data.direction.angle);
        } else {
          this.joystickAngle.emit(0);
          this.joystickDirection.emit('standby');
        }

      }

    });
    manager.on('end', (evt, data) => {
      if (!this.disabled) {
        this.joystickAngle.emit(0);
        this.joystickDirection.emit('standby');
      }
    });
  }
}
