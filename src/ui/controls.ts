import { el } from './components';

export interface ControlsCallbacks {
  onLoadFile: (file: File) => void;
  onLoadUrl: (url: string) => void;
  onRun: () => void;
  onPause: () => void;
  onResume: () => void;
  onCancel: () => void;
  onConcurrencyChange: (n: number) => void;
}

export class Controls {
  readonly element: HTMLElement;

  private fileInput: HTMLInputElement;
  private urlInput: HTMLInputElement;
  private loadUrlBtn: HTMLButtonElement;
  private runBtn: HTMLButtonElement;
  private pauseBtn: HTMLButtonElement;
  private resumeBtn: HTMLButtonElement;
  private cancelBtn: HTMLButtonElement;
  private concurrencySlider: HTMLInputElement;
  private concurrencyLabel: HTMLSpanElement;

  constructor(private callbacks: ControlsCallbacks) {
    this.fileInput = el('input', { type: 'file', accept: '.zip', className: 'file-input' });
    this.fileInput.addEventListener('change', () => {
      const file = this.fileInput.files?.[0];
      if (file) this.callbacks.onLoadFile(file);
    });

    this.urlInput = el('input', {
      type: 'text',
      placeholder: 'test262 zip URL...',
      className: 'url-input',
    });
    this.loadUrlBtn = el('button', { className: 'btn btn-secondary' }, 'Load URL');
    this.loadUrlBtn.addEventListener('click', () => {
      const url = this.urlInput.value.trim();
      if (url) this.callbacks.onLoadUrl(url);
    });

    this.runBtn = el('button', { className: 'btn btn-primary' }, 'Run All');
    this.runBtn.addEventListener('click', () => this.callbacks.onRun());
    this.runBtn.disabled = true;

    this.pauseBtn = el('button', { className: 'btn btn-warning' }, 'Pause');
    this.pauseBtn.addEventListener('click', () => this.callbacks.onPause());
    this.pauseBtn.disabled = true;

    this.resumeBtn = el('button', { className: 'btn btn-success' }, 'Resume');
    this.resumeBtn.addEventListener('click', () => this.callbacks.onResume());
    this.resumeBtn.disabled = true;

    this.cancelBtn = el('button', { className: 'btn btn-danger' }, 'Cancel');
    this.cancelBtn.addEventListener('click', () => this.callbacks.onCancel());
    this.cancelBtn.disabled = true;

    this.concurrencyLabel = el('span', { className: 'concurrency-label' }, '4');
    this.concurrencySlider = el('input', {
      type: 'range',
      min: '1',
      max: '8',
      value: '4',
      className: 'concurrency-slider',
    });
    this.concurrencySlider.addEventListener('input', () => {
      const val = Number(this.concurrencySlider.value);
      this.concurrencyLabel.textContent = String(val);
      this.callbacks.onConcurrencyChange(val);
    });

    const loadRow = el('div', { className: 'controls-row' },
      el('label', { className: 'file-label' },
        el('span', {}, 'Load ZIP:'),
        this.fileInput,
      ),
      this.urlInput,
      this.loadUrlBtn,
    );

    const actionRow = el('div', { className: 'controls-row' },
      this.runBtn,
      this.pauseBtn,
      this.resumeBtn,
      this.cancelBtn,
      el('label', { className: 'concurrency-control' },
        el('span', {}, 'Workers: '),
        this.concurrencySlider,
        this.concurrencyLabel,
      ),
    );

    this.element = el('div', { className: 'controls' }, loadRow, actionRow);
  }

  setLoaded(loaded: boolean): void {
    this.runBtn.disabled = !loaded;
  }

  setRunning(running: boolean): void {
    this.runBtn.disabled = running;
    this.pauseBtn.disabled = !running;
    this.cancelBtn.disabled = !running;
    this.resumeBtn.disabled = true;
    this.fileInput.disabled = running;
    this.loadUrlBtn.disabled = running;
  }

  setPaused(paused: boolean): void {
    this.pauseBtn.disabled = paused;
    this.resumeBtn.disabled = !paused;
  }

  setFinished(): void {
    this.runBtn.disabled = false;
    this.pauseBtn.disabled = true;
    this.resumeBtn.disabled = true;
    this.cancelBtn.disabled = true;
    this.fileInput.disabled = false;
    this.loadUrlBtn.disabled = false;
  }
}
