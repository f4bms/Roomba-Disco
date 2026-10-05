import { ComponentFixture, TestBed } from '@angular/core/testing';

import { MapViewComponent } from './map-view.component';

describe('MapViewComponent', () => {
  let component: MapViewComponent;
  let fixture: ComponentFixture<MapViewComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [MapViewComponent]
    })
    .compileComponents();

    fixture = TestBed.createComponent(MapViewComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('renders each map state and supports the maximum 50 by 50 dimensions', () => {
    fixture.componentRef.setInput('width', 50);
    fixture.componentRef.setInput('height', 50);
    fixture.componentRef.setInput('cells', Array.from({ length: 2500 }, (_, index) => index % 4));
    fixture.detectChanges();

    const grid: HTMLElement = fixture.nativeElement.querySelector('.map-grid');
    const cells = fixture.nativeElement.querySelectorAll('.map-cell');
    const computedGrid = getComputedStyle(grid);

    expect(cells.length).toBe(2500);
    expect(computedGrid.gridTemplateColumns.trim().split(/\s+/).length).toBe(50);
    expect(computedGrid.gridTemplateRows.trim().split(/\s+/).length).toBe(50);
    expect(cells[0].classList).toContain('unknown');
    expect(cells[1].classList).toContain('visited');
    expect(cells[2].classList).toContain('obstacle');
    expect(cells[3].classList).toContain('free-observed');
  });
});
