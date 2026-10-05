import { inject } from '@angular/core';
import { CanActivateFn, Router } from '@angular/router';
import { RobotSocketService } from '../services/robot-socket.service';

// Protege las rutas de control: sin sesion autenticada redirige a /login.
export const authGuard: CanActivateFn = () => {
  const socket = inject(RobotSocketService);
  const router = inject(Router);
  return socket.authState() === 'autenticado' ? true : router.createUrlTree(['/login']);
};
