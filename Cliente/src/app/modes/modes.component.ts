import { Component, inject } from '@angular/core';
import { MatGridListModule } from '@angular/material/grid-list';
import { BreakpointObserver, Breakpoints } from '@angular/cdk/layout';
import { map, shareReplay } from 'rxjs/operators';
import { Observable } from 'rxjs';
import { AsyncPipe } from '@angular/common';
import { LoopComponent } from "./loop/loop.component";

@Component({
  selector: 'component-modes',
  imports: [
    MatGridListModule,
    AsyncPipe,
    LoopComponent
],
  templateUrl: './modes.component.html',
  styleUrl: './modes.component.scss'
})
export class ModesComponent {

  private breakpointObserver = inject(BreakpointObserver);

  isHandset$: Observable<boolean> = this.breakpointObserver.observe(Breakpoints.Handset)
    .pipe(
      map(result => result.matches),
      shareReplay()
    );

}
