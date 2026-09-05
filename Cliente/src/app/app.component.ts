import { Component } from '@angular/core';
import { NavbarComponent } from "./menu/navbar/navbar.component";

@Component({
  selector: 'app-root',
  imports: [NavbarComponent],
  templateUrl: './app.component.html',
  styleUrl: './app.component.scss'
})
export class AppComponent {
  title = 'SCRAP-E_Controller';
}
