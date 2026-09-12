import { ComponentFixture, TestBed } from '@angular/core/testing';

import { AudioPanelComponent } from './audio-panel.component';

describe('AudioPanelComponent', () => {
  let component: AudioPanelComponent;
  let fixture: ComponentFixture<AudioPanelComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [AudioPanelComponent]
    })
    .compileComponents();

    fixture = TestBed.createComponent(AudioPanelComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
