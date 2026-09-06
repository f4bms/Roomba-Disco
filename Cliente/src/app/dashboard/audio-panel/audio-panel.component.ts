import { Component } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatIconModule } from '@angular/material/icon';
import { MatSliderModule } from '@angular/material/slider';

type AudioStatus = 'playing' | 'paused' | 'stopped';

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

  tracks: string[] = ['Pista 1', 'Pista 2', 'Pista 3', 'Pista 4'];
  currentTrack = 0;
  status: AudioStatus = 'stopped';
  volume = 50;

  togglePlay() {
    this.status = this.status === 'playing' ? 'paused' : 'playing';
  }

  stop() {
    this.status = 'stopped';
  }

  prev() {
    if (this.tracks.length === 0) {
      return;
    }
    this.currentTrack = (this.currentTrack - 1 + this.tracks.length) % this.tracks.length;
  }

  next() {
    if (this.tracks.length === 0) {
      return;
    }
    this.currentTrack = (this.currentTrack + 1) % this.tracks.length;
  }

  onVolumeChange(value: number) {
    this.volume = value;
  }
}
