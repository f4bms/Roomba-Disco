import { ComponentFixture, TestBed } from '@angular/core/testing';

import { JoystickComponent } from './joystick.component';
import nipplejs from 'nipplejs';

describe('JoystickComponent', () => {
  let component: JoystickComponent;
  let fixture: ComponentFixture<JoystickComponent>;
  let handlers: Record<string, (event: unknown, data?: unknown) => void>;

  beforeEach(async () => {
    handlers = {};
    spyOn(nipplejs, 'create').and.returnValue({
      on: (event: string, handler: (event: unknown, data?: unknown) => void) => {
        handlers[event] = handler;
      },
    } as ReturnType<typeof nipplejs.create>);
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

  it('emits a direction only when it changes and stops once on release', () => {
    const directions: string[] = [];
    component.joystickDirection.subscribe(direction => directions.push(direction));

    handlers['move'](null, { direction: { angle: 'right' }, angle: { degree: 0 } });
    handlers['move'](null, { direction: { angle: 'right' }, angle: { degree: 1 } });
    handlers['move'](null, { direction: { angle: 'up' }, angle: { degree: 90 } });
    handlers['end'](null);
    handlers['end'](null);

    expect(directions).toEqual(['right', 'up', 'standby']);
  });
});
