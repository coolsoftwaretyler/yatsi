import { YatsiEngine } from './wasm-engine';
import type { WorkerRequest, WorkerResponse } from '../types/engine';

let engine: YatsiEngine | null = null;

async function init() {
  engine = await YatsiEngine.create();
  const msg: WorkerResponse = { type: 'ready' };
  self.postMessage(msg);
}

self.onmessage = async (e: MessageEvent<WorkerRequest>) => {
  const req = e.data;

  if (req.type === 'evaluate') {
    if (!engine) {
      throw new Error('Engine not initialized');
    }
    const result = engine.evaluate(req.source!);
    const msg: WorkerResponse = {
      type: 'result',
      taskId: req.taskId,
      result,
    };
    self.postMessage(msg);
  } else if (req.type === 'reset') {
    if (engine) {
      engine.destroy();
    }
    engine = await YatsiEngine.create();
    const msg: WorkerResponse = { type: 'ready' };
    self.postMessage(msg);
  }
};

init();
