import { Component } from '@angular/core';
import { MatCardModule } from '@angular/material/card';

@Component({
  selector: 'app-map-view',
  imports: [MatCardModule],
  templateUrl: './map-view.component.html',
  styleUrl: './map-view.component.scss',
})
export class MapViewComponent {

  width = 8;
  height = 6;
  cells: number[] = [];

  constructor() {
    this.cells = Array.from({ length: this.width * this.height }, () => 0);
  }
}
