import { ComponentFixture, TestBed } from '@angular/core/testing';

import { JoystickComponent } from './joystick.component';

describe('JoystickComponent', () => {
  let component: JoystickComponent;
  let fixture: ComponentFixture<JoystickComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [JoystickComponent]
    })
    .compileComponents();

    fixture = TestBed.createComponent(JoystickComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
