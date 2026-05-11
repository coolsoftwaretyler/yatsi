import { el, clearChildren } from './components';
import type { TestResult } from '../types/test262';

export interface TestSourceInfo {
  source: string;
  expected: string;
}

export class ResultsPanel {
  readonly element: HTMLElement;

  private headerEl: HTMLElement;
  private list: HTMLElement;
  private results: TestResult[] = [];
  private maxVisible = 200;
  private sourceMap: Map<string, TestSourceInfo> | null = null;
  private showAll = false;

  constructor() {
    this.headerEl = el('h3', {}, 'Failures');
    this.list = el('div', { className: 'failure-list' });
    this.element = el('div', { className: 'results-panel' }, this.headerEl, this.list);
  }

  setSourceMap(map: Map<string, TestSourceInfo>): void {
    this.sourceMap = map;
    this.showAll = true;
    this.headerEl.textContent = 'Results';
  }

  reset(): void {
    this.results = [];
    clearChildren(this.list);
  }

  addResult(result: TestResult): void {
    if (!this.showAll && result.outcome !== 'fail' && result.outcome !== 'timeout') return;

    this.results.push(result);

    if (this.results.length <= this.maxVisible) {
      this.list.appendChild(this.renderResult(result));
    } else if (this.results.length === this.maxVisible + 1) {
      this.list.appendChild(
        el('div', { className: 'failure-overflow' },
          `Too many results to display. Showing first ${this.maxVisible}.`),
      );
    }
  }

  private renderResult(result: TestResult): HTMLElement {
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

    const sourceInfo = this.sourceMap?.get(result.testPath);
    if (sourceInfo) {
      details.appendChild(el('div', { className: 'source-section' },
        el('div', { className: 'source-label' }, 'Source:'),
        el('pre', { className: 'source-code' }, sourceInfo.source.trim()),
      ));
      details.appendChild(el('div', { className: 'source-section' },
        el('div', { className: 'source-label' }, 'Expected output:'),
        el('pre', { className: 'source-code' }, sourceInfo.expected.trim()),
      ));
    }

    if (result.errorType) {
      details.appendChild(el('div', { className: 'failure-error-type' }, result.errorType));
    }
    if (result.errorMessage) {
      details.appendChild(el('div', { className: 'failure-message' }, result.errorMessage));
    }
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
