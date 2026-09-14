import { Component, effect, inject } from '@angular/core';
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
  powered = true;
  volume = 50;

  constructor() {
    effect(() => {
      const state = this.socket.state();
      if (state === null) return;
      this.powered = state.reported.power;
      this.playing = state.reported.audio.status === 'playing';
      this.volume = state.reported.audio.volume;
      this.currentTrack = state.reported.audio.track;
      this.tracks = state.reported.audio.tracks;
    });
  }

  togglePlay() {
    this.socket.sendDesired({ audio: { action: this.playing ? 'PAUSE' : 'PLAY' } });
  }

  stop() {
    this.socket.sendDesired({ audio: { action: 'STOP' } });
  }

  prev() {
    this.socket.sendDesired({ audio: { action: 'PREV' } });
  }

  next() {
    this.socket.sendDesired({ audio: { action: 'NEXT' } });
  }

  onVolumeChange(value: number) {
    this.socket.sendDesired({ audio: { volume: value } });
  }
}
