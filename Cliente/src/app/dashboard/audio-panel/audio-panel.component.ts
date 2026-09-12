import { Component, inject } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatIconModule } from '@angular/material/icon';
import { MatSliderModule } from '@angular/material/slider';
import { RobotSocketService } from '../../services/robot-socket.service';

@Component({
  selector: 'app-audio-panel',
  imports: [
    MatButtonModule,
    MatCardModule,
    MatIconModule,
    MatSliderModule,
  ],
  templateUrl: './audio-panel.component.html',
  styleUrl: './audio-panel.component.scss',
})
export class AudioPanelComponent {

  private readonly socket = inject(RobotSocketService);

  // Lista real y estado de reproduccion llegaran en AUDIO:STATUS desde Logica.
  tracks: string[] = [];
  currentTrack: number | null = null;

  // Estado local para pruebas: refleja el ultimo comando enviado.
  playing = false;
  volume = 50;

  togglePlay() {
    this.playing = !this.playing;
    this.socket.send(this.playing ? 'AUDIO:PLAY' : 'AUDIO:PAUSE');
  }

  stop() {
    this.playing = false;
    this.socket.send('AUDIO:STOP');
  }

  prev() {
    this.socket.send('AUDIO:PREV');
  }

  next() {
    this.socket.send('AUDIO:NEXT');
  }

  onVolumeChange(value: number) {
    this.volume = value;
    this.socket.send(`AUDIO:VOL:${value}`);
  }
}
