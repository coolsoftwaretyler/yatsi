export type ErrorPhase = 'none' | 'parse' | 'runtime';

export interface EvalResult {
  success: boolean;
  errorType: string;
  errorMessage: string;
  phase: ErrorPhase;
  consoleOutput: string;
}

export interface WorkerRequest {
  type: 'evaluate' | 'reset';
  taskId?: string;
  source?: string;
}

export interface WorkerResponse {
  type: 'ready' | 'result';
  taskId?: string;
  result?: EvalResult;
}
