import type { EvalResult } from '../types/engine';
import type { TestOutcome } from '../types/test262';

export function interpretCustomResult(
  evalResult: EvalResult,
  expectedOutput: string,
): { outcome: TestOutcome; errorType: string; errorMessage: string } {
  if (!evalResult.success) {
    return {
      outcome: 'fail',
      errorType: evalResult.errorType,
      errorMessage: evalResult.errorMessage,
    };
  }

  const actual = evalResult.consoleOutput.trim();
  const expected = expectedOutput.trim();

  if (actual === expected) {
    return { outcome: 'pass', errorType: '', errorMessage: '' };
  }

  return {
    outcome: 'fail',
    errorType: 'OutputMismatch',
    errorMessage: `Expected:\n${expected}\n\nActual:\n${actual}`,
  };
}
