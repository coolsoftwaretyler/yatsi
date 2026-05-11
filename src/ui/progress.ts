import { el } from './components';
import type { TestCounts } from '../types/test262';

export class ProgressPanel {
  readonly element: HTMLElement;

  private bar: HTMLElement;
  private barFill: HTMLElement;
  private statsEl: HTMLElement;
  private counts: TestCounts = { pass: 0, fail: 0, skip: 0, timeout: 0, total: 0 };

  constructor() {
    this.barFill = el('div', { className: 'progress-fill' });
    this.bar = el('div', { className: 'progress-bar' }, this.barFill);
    this.statsEl = el('div', { className: 'progress-stats' });
    this.element = el('div', { className: 'progress-panel' }, this.bar, this.statsEl);
    this.render();
  }

  reset(total: number): void {
    this.counts = { pass: 0, fail: 0, skip: 0, timeout: 0, total };
    this.render();
  }

  update(counts: TestCounts): void {
    this.counts = counts;
    this.render();
  }

  private render(): void {
    const { pass, fail, skip, timeout, total } = this.counts;
    const completed = pass + fail + skip + timeout;
    const pct = total > 0 ? (completed / total) * 100 : 0;

    this.barFill.style.width = `${pct}%`;

    // Color the bar based on results
    if (fail > 0 || timeout > 0) {
      this.barFill.className = 'progress-fill progress-fill-fail';
    } else if (pass > 0) {
      this.barFill.className = 'progress-fill progress-fill-pass';
    } else {
      this.barFill.className = 'progress-fill';
    }

    this.statsEl.textContent =
      `${completed}/${total} — ` +
      `Pass: ${pass} | Fail: ${fail} | Skip: ${skip} | Timeout: ${timeout}`;
  }
}
