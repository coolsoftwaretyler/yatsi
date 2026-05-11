import { el, siteFooter } from './components';
import { Controls } from './controls';
import { ProgressPanel } from './progress';
import { TestTree } from './test-tree';
import { ResultsPanel } from './results-panel';
import { loadFromFile, loadFromUrl } from '../test262/loader';
import { parseFrontmatter } from '../test262/frontmatter';
import { assembleTest, determineScenarios } from '../test262/assembler';
import { TestExecutor } from '../test262/executor';
import type { TestSuiteBundle, TestResult, TestCounts, AssembledTest, TestTreeNode, TestEntry } from '../types/test262';

export class App {
  private controls: Controls;
  private progress: ProgressPanel;
  private testTree: TestTree;
  private resultsPanel: ResultsPanel;
  private statusEl: HTMLElement;

  private bundle: TestSuiteBundle | null = null;
  private executor: TestExecutor | null = null;
  private counts: TestCounts = { pass: 0, fail: 0, skip: 0, timeout: 0, total: 0 };

  constructor(private root: HTMLElement) {
    this.controls = new Controls({
      onLoadFile: (f) => this.handleLoadFile(f),
      onLoadUrl: (u) => this.handleLoadUrl(u),
      onRun: () => this.handleRun(),
      onPause: () => this.handlePause(),
      onResume: () => this.handleResume(),
      onCancel: () => this.handleCancel(),
      onConcurrencyChange: (n) => this.executor?.setConcurrency(n),
    });

    this.progress = new ProgressPanel();
    this.testTree = new TestTree();
    this.resultsPanel = new ResultsPanel();
    this.statusEl = el('div', { className: 'status' });
  }

  init(): void {
    const nav = el('nav', { className: 'app-nav' },
      el('a', { href: '../' }, 'Home'),
      el('a', { href: '../custom/' }, 'Custom Tests'),
      el('a', { href: '../playground/' }, 'Playground'),
    );

    const header = el('header', { className: 'app-header' },
      el('h1', {}, 'Yatsi'),
      el('p', { className: 'subtitle' }, 'Test262 Web Runner'),
      nav,
    );

    const main = el('main', { className: 'app-main' },
      this.controls.element,
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
    this.setStatus('Load a test262 zip to begin.');
  }

  private setStatus(msg: string): void {
    this.statusEl.textContent = msg;
  }

  private async handleLoadFile(file: File): Promise<void> {
    this.setStatus(`Loading ${file.name}...`);
    try {
      this.bundle = await loadFromFile(file);
      this.onBundleLoaded();
    } catch (err) {
      this.setStatus(`Error: ${(err as Error).message}`);
    }
  }

  private async handleLoadUrl(url: string): Promise<void> {
    this.setStatus('Downloading...');
    try {
      this.bundle = await loadFromUrl(url, (loaded, total) => {
        const pct = ((loaded / total) * 100).toFixed(1);
        this.setStatus(`Downloading... ${pct}%`);
      });
      this.onBundleLoaded();
    } catch (err) {
      this.setStatus(`Error: ${(err as Error).message}`);
    }
  }

  private onBundleLoaded(): void {
    if (!this.bundle) return;
    this.setStatus(`Loaded ${this.bundle.testCount} tests. Click "Run All" to start.`);
    this.controls.setLoaded(true);
    this.testTree.render(this.bundle.tree);
  }

  private async handleRun(): Promise<void> {
    if (!this.bundle) return;

    this.setStatus('Assembling tests...');
    this.counts = { pass: 0, fail: 0, skip: 0, timeout: 0, total: 0 };
    this.resultsPanel.reset();

    const assembled = await this.assembleAllTests();
    this.counts.total = assembled.length;
    this.progress.reset(assembled.length);
    this.controls.setRunning(true);
    this.setStatus(`Running ${assembled.length} test scenarios...`);

    this.executor = new TestExecutor({
      onResult: (result: TestResult) => this.handleResult(result),
      onProgress: (completed: number, total: number) => {
        this.progress.update(this.counts);
        if (completed === total) {
          this.onRunComplete();
        }
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
    this.controls.setFinished();
    const { pass, fail, skip, timeout, total } = this.counts;
    this.setStatus(
      `Complete: ${total} scenarios — ${pass} pass, ${fail} fail, ${skip} skip, ${timeout} timeout`,
    );
  }

  private handlePause(): void {
    this.executor?.pause();
    this.controls.setPaused(true);
    this.setStatus('Paused.');
  }

  private handleResume(): void {
    this.executor?.resume();
    this.controls.setPaused(false);
    this.setStatus('Running...');
  }

  private handleCancel(): void {
    this.executor?.cancel();
    this.controls.setFinished();
    this.setStatus('Cancelled.');
  }

  private async assembleAllTests(): Promise<AssembledTest[]> {
    if (!this.bundle) return [];

    const assembled: AssembledTest[] = [];
    const allTests = this.collectAllTests(this.bundle.tree);

    for (const entry of allTests) {
      const source = await this.bundle.getFileContent(entry.path);
      const metadata = parseFrontmatter(source);
      const scenarios = determineScenarios(metadata.flags);

      for (const scenario of scenarios) {
        assembled.push(
          assembleTest(entry.relativePath, source, metadata, this.bundle.harness, scenario),
        );
      }
    }

    return assembled;
  }

  private collectAllTests(node: TestTreeNode): TestEntry[] {
    const tests: TestEntry[] = [...node.tests];
    for (const child of node.children.values()) {
      tests.push(...this.collectAllTests(child));
    }
    return tests;
  }
}
