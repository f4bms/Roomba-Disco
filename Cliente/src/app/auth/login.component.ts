import { Component, inject } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { Router } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { RobotSocketService } from '../services/robot-socket.service';

@Component({
  selector: 'app-login',
  imports: [
    FormsModule,
    MatButtonModule,
    MatCardModule,
    MatFormFieldModule,
    MatInputModule,
    MatProgressSpinnerModule,
  ],
  templateUrl: './login.component.html',
  styleUrl: './login.component.scss',
})
export class LoginComponent {

  private readonly socket = inject(RobotSocketService);
  private readonly router = inject(Router);

  readonly socketAddress = this.socket.serverAddress;

  user = '';
  password = '';
  error = '';
  busy = false;

  onServerChange(event: Event) {
    this.socket.setServerAddress((event.target as HTMLInputElement).value);
  }

  async submit() {
    if (this.busy || this.user.trim() === '' || this.password === '') return;
    this.busy = true;
    this.error = '';
    const ok = await this.socket.login(this.user.trim(), this.password);
    this.busy = false;
    this.password = '';
    if (ok) {
      this.router.navigate(['/']);
    } else {
      this.error = 'Usuario o contraseña incorrectos, o servidor no disponible.';
    }
  }
}
