import { Routes } from '@angular/router';
import { DashboardComponent } from './dashboard/dashboard.component';
import { ControllerComponent } from './controller/controller.component';
import { ModesComponent } from './modes/modes.component';
import { LoginComponent } from './auth/login.component';
import { authGuard } from './auth/auth.guard';

export const routes: Routes = [
  { path: 'login', component: LoginComponent },
  { path: '', component: DashboardComponent, canActivate: [authGuard] },
  { path: 'controller', component: ControllerComponent, canActivate: [authGuard] },
  { path: 'modes', component: ModesComponent, canActivate: [authGuard] },
  { path: '**', redirectTo: 'login' },
];

