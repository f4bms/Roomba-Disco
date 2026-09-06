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
    // Datos de ejemplo para visualizar el mapa (0=desconocida, 1=visitada, 2=obstáculo).
    const visited = [9, 10, 11, 17, 18, 19, 25, 26, 27];
    const obstacles = [12, 20, 28];
    visited.forEach((index) => (this.cells[index] = 1));
    obstacles.forEach((index) => (this.cells[index] = 2));
  }
}
