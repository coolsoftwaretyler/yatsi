import type { AssembledTest, TestResult } from '../types/test262';
import type { EvalResult, WorkerResponse } from '../types/engine';
import { interpretResult } from './result-interpreter';

export type ResultCallback = (result: TestResult) => void;
export type ProgressCallback = (completed: number, total: number) => void;
export type ResultInterpreter = (
  evalResult: EvalResult,
  test: AssembledTest,
) => { outcome: TestResult['outcome']; errorType: string; errorMessage: string };

interface PendingTask {
  test: AssembledTest;
  taskId: string;
  startTime: number;
  timeoutId: ReturnType<typeof setTimeout>;
}

interface WorkerSlot {
  worker: Worker;
  busy: boolean;
  pending: PendingTask | null;
}

export class TestExecutor {
  private workers: WorkerSlot[] = [];
  private queue: AssembledTest[] = [];
  private completed = 0;
  private total = 0;
  private taskCounter = 0;
  private paused = false;
  private cancelled = false;
  private readyResolvers: Map<Worker, () => void> = new Map();

  private onResult: ResultCallback;
  private onProgress: ProgressCallback;
  private timeout: number;
  private concurrency: number;
  private customInterpreter?: ResultInterpreter;

  constructor(opts: {
    concurrency?: number;
    timeout?: number;
    onResult: ResultCallback;
    onProgress: ProgressCallback;
    interpreter?: ResultInterpreter;
  }) {
    this.concurrency = opts.concurrency ?? 4;
    this.timeout = opts.timeout ?? 10000;
    this.onResult = opts.onResult;
    this.onProgress = opts.onProgress;
    this.customInterpreter = opts.interpreter;
  }

  async start(tests: AssembledTest[]): Promise<void> {
    this.queue = [...tests];
    this.total = tests.length;
    this.completed = 0;
    this.paused = false;
    this.cancelled = false;

    await this.spawnWorkers();
    this.dispatch();
  }

  pause(): void {
    this.paused = true;
  }

  resume(): void {
    if (!this.paused) return;
    this.paused = false;
    this.dispatch();
  }

  cancel(): void {
    this.cancelled = true;
    this.queue = [];
    for (const slot of this.workers) {
      if (slot.pending) {
        clearTimeout(slot.pending.timeoutId);
      }
      slot.worker.terminate();
    }
    this.workers = [];
  }

  setConcurrency(n: number): void {
    this.concurrency = Math.max(1, Math.min(8, n));
  }

  destroy(): void {
    this.cancel();
  }

  private async spawnWorkers(): Promise<void> {
    const workerCount = Math.min(this.concurrency, this.total);
    const readyPromises: Promise<void>[] = [];

    for (let i = 0; i < workerCount; i++) {
      const worker = new Worker(
        new URL('../engine/engine.worker.ts', import.meta.url),
        { type: 'module' },
      );

      const slot: WorkerSlot = { worker, busy: false, pending: null };
      this.workers.push(slot);

      const readyPromise = new Promise<void>(resolve => {
        this.readyResolvers.set(worker, resolve);
      });
      readyPromises.push(readyPromise);

      worker.onmessage = (e: MessageEvent<WorkerResponse>) => {
        this.handleMessage(slot, e.data);
      };

      worker.onerror = (e: ErrorEvent) => {
        e.preventDefault();
        this.handleWorkerCrash(slot);
      };
    }

    await Promise.all(readyPromises);
  }

  private handleMessage(slot: WorkerSlot, msg: WorkerResponse): void {
    if (msg.type === 'ready') {
      const resolver = this.readyResolvers.get(slot.worker);
      if (resolver) {
        resolver();
        this.readyResolvers.delete(slot.worker);
      }
      return;
    }

    if (msg.type === 'result' && slot.pending) {
      const task = slot.pending;

      // Verify taskId matches
      if (msg.taskId !== task.taskId) return;

      clearTimeout(task.timeoutId);
      slot.busy = false;
      slot.pending = null;

      const elapsed = performance.now() - task.startTime;
      const interpretation = this.customInterpreter
        ? this.customInterpreter(msg.result!, task.test)
        : interpretResult(msg.result!, task.test.metadata, task.test.scenario);

      const result: TestResult = {
        testPath: task.test.testPath,
        scenario: task.test.scenario,
        outcome: interpretation.outcome,
        errorType: interpretation.errorType,
        errorMessage: interpretation.errorMessage,
        duration: elapsed,
      };

      this.completed++;
      this.onResult(result);
      this.onProgress(this.completed, this.total);

      if (!this.cancelled) {
        this.dispatch();
      }
    }
  }

  private dispatch(): void {
    if (this.paused || this.cancelled) return;

    for (const slot of this.workers) {
      if (slot.busy || this.queue.length === 0) continue;
      if (this.paused || this.cancelled) break;

      const test = this.queue.shift()!;
      this.dispatchToSlot(slot, test);
    }
  }

  private dispatchToSlot(slot: WorkerSlot, test: AssembledTest): void {
    const taskId = String(++this.taskCounter);
    slot.busy = true;

    const timeoutId = setTimeout(() => {
      this.handleTimeout(slot, test, taskId);
    }, this.timeout);

    slot.pending = {
      test,
      taskId,
      startTime: performance.now(),
      timeoutId,
    };

    slot.worker.postMessage({
      type: 'evaluate',
      taskId,
      source: test.source,
      mode: test.mode,
    });
  }

  private async handleTimeout(slot: WorkerSlot, test: AssembledTest, taskId: string): Promise<void> {
    if (slot.pending?.taskId !== taskId) return;

    slot.pending = null;
    slot.busy = false;

    // Terminate and replace the timed-out worker
    slot.worker.terminate();

    const newWorker = new Worker(
      new URL('../engine/engine.worker.ts', import.meta.url),
      { type: 'module' },
    );

    const readyPromise = new Promise<void>(resolve => {
      this.readyResolvers.set(newWorker, resolve);
    });

    newWorker.onmessage = (e: MessageEvent<WorkerResponse>) => {
      this.handleMessage(slot, e.data);
    };

    newWorker.onerror = (e: ErrorEvent) => {
      e.preventDefault();
      this.handleWorkerCrash(slot);
    };

    slot.worker = newWorker;

    await readyPromise;

    const result: TestResult = {
      testPath: test.testPath,
      scenario: test.scenario,
      outcome: 'timeout',
      errorType: 'TimeoutError',
      errorMessage: `Test exceeded ${this.timeout}ms timeout`,
      duration: this.timeout,
    };

    this.completed++;
    this.onResult(result);
    this.onProgress(this.completed, this.total);

    if (!this.cancelled) {
      this.dispatch();
    }
  }

  private async handleWorkerCrash(slot: WorkerSlot): Promise<void> {
    const task = slot.pending;
    if (task) {
      clearTimeout(task.timeoutId);
      slot.pending = null;
      slot.busy = false;

      const elapsed = performance.now() - task.startTime;
      const result: TestResult = {
        testPath: task.test.testPath,
        scenario: task.test.scenario,
        outcome: 'fail',
        errorType: 'InternalError',
        errorMessage: 'Worker crashed (WASM abort)',
        duration: elapsed,
      };

      this.completed++;
      this.onResult(result);
      this.onProgress(this.completed, this.total);
    }

    // Terminate and replace the crashed worker
    slot.worker.terminate();

    const newWorker = new Worker(
      new URL('../engine/engine.worker.ts', import.meta.url),
      { type: 'module' },
    );

    const readyPromise = new Promise<void>(resolve => {
      this.readyResolvers.set(newWorker, resolve);
    });

    newWorker.onmessage = (e: MessageEvent<WorkerResponse>) => {
      this.handleMessage(slot, e.data);
    };

    newWorker.onerror = (e: ErrorEvent) => {
      e.preventDefault();
      this.handleWorkerCrash(slot);
    };

    slot.worker = newWorker;

    await readyPromise;

    if (!this.cancelled) {
      this.dispatch();
    }
  }
}
