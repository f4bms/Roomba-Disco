import { Component, Input } from '@angular/core';
import { MatCardModule } from '@angular/material/card';

@Component({
  selector: 'app-map-view',
  imports: [MatCardModule],
  templateUrl: './map-view.component.html',
  styleUrl: './map-view.component.scss',
})
export class MapViewComponent {

  @Input() width = 8;
  @Input() height = 6;
  @Input() cells: number[] = Array.from({ length: 48 }, () => 0);
}
