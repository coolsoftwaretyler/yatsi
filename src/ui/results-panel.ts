import { el, clearChildren } from './components';
import type { TestResult } from '../types/test262';

export class ResultsPanel {
  readonly element: HTMLElement;

  private list: HTMLElement;
  private failures: TestResult[] = [];
  private maxVisible = 200;

  constructor() {
    const header = el('h3', {}, 'Failures');
    this.list = el('div', { className: 'failure-list' });
    this.element = el('div', { className: 'results-panel' }, header, this.list);
  }

  reset(): void {
    this.failures = [];
    clearChildren(this.list);
  }

  addResult(result: TestResult): void {
    if (result.outcome !== 'fail' && result.outcome !== 'timeout') return;

    this.failures.push(result);

    if (this.failures.length <= this.maxVisible) {
      this.list.appendChild(this.renderFailure(result));
    } else if (this.failures.length === this.maxVisible + 1) {
      this.list.appendChild(
        el('div', { className: 'failure-overflow' },
          'Too many failures to display. Showing first 200.'),
      );
    }
  }

  private renderFailure(result: TestResult): HTMLElement {
    const item = el('div', { className: 'failure-item' });

    const header = el('div', { className: 'failure-header' });
    const path = el('span', { className: 'failure-path' }, result.testPath);
    const scenario = el('span', { className: 'failure-scenario' }, `[${result.scenario}]`);
    const outcome = el('span', {
      className: `failure-outcome failure-outcome-${result.outcome}`,
    }, result.outcome.toUpperCase());

    header.appendChild(path);
    header.appendChild(scenario);
    header.appendChild(outcome);

    const details = el('div', { className: 'failure-details hidden' });
    if (result.errorType) {
      details.appendChild(el('div', { className: 'failure-error-type' }, result.errorType));
    }
    details.appendChild(el('div', { className: 'failure-message' }, result.errorMessage));
    details.appendChild(
      el('div', { className: 'failure-duration' }, `${result.duration.toFixed(1)}ms`),
    );

    header.style.cursor = 'pointer';
    header.addEventListener('click', () => {
      details.classList.toggle('hidden');
    });

    item.appendChild(header);
    item.appendChild(details);
    return item;
  }
}
