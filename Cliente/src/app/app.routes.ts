import { Routes } from '@angular/router';
import { DashboardComponent } from './dashboard/dashboard.component';
import { ControllerComponent } from './controller/controller.component';
import { ModesComponent } from './modes/modes.component';

export const routes: Routes = [
  { path: '', component: DashboardComponent },
  { path: 'controller', component: ControllerComponent },
  { path: 'modes', component: ModesComponent },
];

