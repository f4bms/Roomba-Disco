import { Routes } from '@angular/router';
import { ControllerComponent } from './controller/controller.component';
import { ModesComponent } from './modes/modes.component';

export const routes: Routes = [

  { path: 'controller', component: ControllerComponent },
  { path: 'modes', component: ModesComponent },
];

