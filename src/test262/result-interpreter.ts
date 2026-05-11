import type { TestMetadata, TestOutcome, TestScenario } from '../types/test262';
import type { EvalResult } from '../types/engine';

export function interpretResult(
  evalResult: EvalResult,
  metadata: TestMetadata,
  scenario: TestScenario,
): { outcome: TestOutcome; errorType: string; errorMessage: string } {
  // Module tests: skip for now
  if (scenario === 'module') {
    return { outcome: 'skip', errorType: '', errorMessage: 'Module tests not yet supported' };
  }

  // Async tests: check console output
  if (metadata.flags.includes('async')) {
    return interpretAsync(evalResult);
  }

  // Negative tests: expect specific error at specific phase
  if (metadata.negative) {
    return interpretNegative(evalResult, metadata.negative);
  }

  // Normal tests: success = pass, error = fail
  return interpretNormal(evalResult);
}

function interpretNormal(result: EvalResult): { outcome: TestOutcome; errorType: string; errorMessage: string } {
  if (result.success) {
    return { outcome: 'pass', errorType: '', errorMessage: '' };
  }
  return {
    outcome: 'fail',
    errorType: result.errorType,
    errorMessage: result.errorMessage,
  };
}

function interpretNegative(
  result: EvalResult,
  expected: { phase: string; type: string },
): { outcome: TestOutcome; errorType: string; errorMessage: string } {
  // Negative test should fail with the expected error
  if (result.success) {
    return {
      outcome: 'fail',
      errorType: '',
      errorMessage: `Expected ${expected.type} during ${expected.phase} but test passed`,
    };
  }

  const phaseMatch = matchPhase(result.phase, expected.phase);
  const typeMatch = result.errorType === expected.type;

  if (phaseMatch && typeMatch) {
    return { outcome: 'pass', errorType: '', errorMessage: '' };
  }

  return {
    outcome: 'fail',
    errorType: result.errorType,
    errorMessage: `Expected ${expected.type} during ${expected.phase}, got ${result.errorType} during ${result.phase}`,
  };
}

function interpretAsync(result: EvalResult): { outcome: TestOutcome; errorType: string; errorMessage: string } {
  if (!result.success) {
    return {
      outcome: 'fail',
      errorType: result.errorType,
      errorMessage: result.errorMessage,
    };
  }

  const output = result.consoleOutput;
  if (output.includes('Test262:AsyncTestComplete')) {
    return { outcome: 'pass', errorType: '', errorMessage: '' };
  }
  if (output.includes('Test262:AsyncTestFailure')) {
    const failMatch = output.match(/Test262:AsyncTestFailure:(.*)/);
    return {
      outcome: 'fail',
      errorType: 'AsyncTestFailure',
      errorMessage: failMatch ? failMatch[1].trim() : 'Async test failed',
    };
  }

  return {
    outcome: 'fail',
    errorType: 'AsyncTestIncomplete',
    errorMessage: 'Async test did not signal completion',
  };
}

function matchPhase(actual: string, expected: string): boolean {
  // test262 phases: "parse" (early errors), "resolution" (module), "runtime"
  // Our engine phases: "none", "parse", "runtime"
  const mapping: Record<string, string> = {
    parse: 'parse',
    early: 'parse',
    resolution: 'parse',
    runtime: 'runtime',
  };
  return actual === (mapping[expected] ?? expected);
}
