import { el, siteFooter } from './components';
import { ProgressPanel } from './progress';
import { TestTree } from './test-tree';
import { ResultsPanel, type TestSourceInfo } from './results-panel';
import { TestExecutor } from '../test262/executor';
import { loadEmbeddedTests } from '../custom/loader';
import { interpretCustomResult } from '../custom/result-interpreter';
import type { CustomTest } from '../custom/loader';
import type { TestResult, TestCounts, AssembledTest, TestMetadata } from '../types/test262';
import type { EvalResult } from '../types/engine';

const EMPTY_METADATA: TestMetadata = {
  description: '',
  features: [],
  flags: [],
  includes: [],
  negative: null,
  locale: [],
};

export class CustomApp {
  private progress: ProgressPanel;
  private testTree: TestTree;
  private resultsPanel: ResultsPanel;
  private statusEl: HTMLElement;
  private runBtn: HTMLButtonElement;
  private cancelBtn: HTMLButtonElement;

  private executor: TestExecutor | null = null;
  private counts: TestCounts = { pass: 0, fail: 0, skip: 0, timeout: 0, total: 0 };
  private tests: CustomTest[] = [];
  private expectedByPath = new Map<string, string>();

  constructor(private root: HTMLElement) {
    this.progress = new ProgressPanel();
    this.testTree = new TestTree();
    this.resultsPanel = new ResultsPanel();
    this.statusEl = el('div', { className: 'status' });

    this.runBtn = el('button', { className: 'btn btn-primary' }, 'Run All');
    this.runBtn.addEventListener('click', () => this.handleRun());

    this.cancelBtn = el('button', { className: 'btn btn-danger' }, 'Cancel');
    this.cancelBtn.addEventListener('click', () => this.handleCancel());
    this.cancelBtn.disabled = true;
  }

  init(): void {
    const nav = el('nav', { className: 'app-nav' },
      el('a', { href: '../' }, 'Home'),
      el('a', { href: '../test262/' }, 'Test262 Runner'),
      el('a', { href: '../playground/' }, 'Playground'),
    );

    const header = el('header', { className: 'app-header' },
      el('h1', {}, 'Yatsi'),
      el('p', { className: 'subtitle' }, 'Custom Tests'),
      nav,
    );

    const controlsRow = el('div', { className: 'controls-row' },
      this.runBtn,
      this.cancelBtn,
    );

    const main = el('main', { className: 'app-main' },
      el('div', { className: 'controls' }, controlsRow),
      this.statusEl,
      this.progress.element,
      el('div', { className: 'app-content' },
        el('div', { className: 'tree-pane' }, this.testTree.element),
        el('div', { className: 'results-pane' }, this.resultsPanel.element),
      ),
    );

    this.root.appendChild(header);
    this.root.appendChild(main);
    this.root.appendChild(siteFooter());

    this.loadTests();
  }

  private loadTests(): void {
    const suite = loadEmbeddedTests();
    this.tests = suite.tests;
    this.testTree.render(suite.tree);

    const sourceMap = new Map<string, TestSourceInfo>();
    for (const test of this.tests) {
      this.expectedByPath.set(test.relativePath, test.expected);
      sourceMap.set(test.relativePath, { source: test.source, expected: test.expected });
    }
    this.resultsPanel.setSourceMap(sourceMap);

    this.setStatus(`Loaded ${this.tests.length} tests. Click "Run All" to start.`);
  }

  private setStatus(msg: string): void {
    this.statusEl.textContent = msg;
  }

  private async handleRun(): Promise<void> {
    this.counts = { pass: 0, fail: 0, skip: 0, timeout: 0, total: 0 };
    this.resultsPanel.reset();

    const assembled: AssembledTest[] = this.tests.map(t => ({
      testPath: t.relativePath,
      scenario: 'non-strict' as const,
      source: t.source,
      metadata: EMPTY_METADATA,
    }));

    this.counts.total = assembled.length;
    this.progress.reset(assembled.length);
    this.runBtn.disabled = true;
    this.cancelBtn.disabled = false;
    this.setStatus(`Running ${assembled.length} tests...`);

    this.executor = new TestExecutor({
      concurrency: 4,
      timeout: 10000,
      onResult: (result: TestResult) => this.handleResult(result),
      onProgress: (completed: number, total: number) => {
        this.progress.update(this.counts);
        if (completed === total) {
          this.onRunComplete();
        }
      },
      interpreter: (evalResult: EvalResult, test: AssembledTest) => {
        const expected = this.expectedByPath.get(test.testPath) ?? '';
        return interpretCustomResult(evalResult, expected);
      },
    });

    await this.executor.start(assembled);
  }

  private handleResult(result: TestResult): void {
    if (result.outcome === 'pass') this.counts.pass++;
    else if (result.outcome === 'fail') this.counts.fail++;
    else if (result.outcome === 'skip') this.counts.skip++;
    else if (result.outcome === 'timeout') this.counts.timeout++;

    this.testTree.updateResult(result);
    this.resultsPanel.addResult(result);
  }

  private onRunComplete(): void {
    this.runBtn.disabled = false;
    this.cancelBtn.disabled = true;
    const { pass, fail, skip, timeout, total } = this.counts;
    this.setStatus(
      `Complete: ${total} tests — ${pass} pass, ${fail} fail, ${skip} skip, ${timeout} timeout`,
    );
  }

  private handleCancel(): void {
    this.executor?.cancel();
    this.runBtn.disabled = false;
    this.cancelBtn.disabled = true;
    this.setStatus('Cancelled.');
  }
}
