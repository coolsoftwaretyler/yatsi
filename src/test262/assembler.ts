import type { TestMetadata, TestScenario, AssembledTest } from '../types/test262';
import { YATSI_262_SHIM, REQUIRED_HARNESS_FILES, ASYNC_HARNESS_FILE } from './constants';

export function determineScenarios(flags: string[]): TestScenario[] {
  if (flags.includes('raw')) return ['raw'];
  if (flags.includes('module')) return ['module'];
  if (flags.includes('onlyStrict')) return ['strict'];
  if (flags.includes('noStrict')) return ['non-strict'];
  return ['strict', 'non-strict'];
}

export function assembleTest(
  testPath: string,
  testSource: string,
  metadata: TestMetadata,
  harness: Map<string, string>,
  scenario: TestScenario,
): AssembledTest {
  const parts: string[] = [];
  const isRaw = scenario === 'raw';

  if (!isRaw) {
    // 1. Required harness files
    for (const file of REQUIRED_HARNESS_FILES) {
      const content = harness.get(file);
      if (content) {
        parts.push(content);
      }
    }

    // 2. $262 shim
    parts.push(YATSI_262_SHIM);

    // 3. Async harness if needed
    if (metadata.flags.includes('async')) {
      const content = harness.get(ASYNC_HARNESS_FILE);
      if (content) {
        parts.push(content);
      }
    }

    // 4. Additional includes
    for (const include of metadata.includes) {
      const content = harness.get(include);
      if (content) {
        parts.push(content);
      }
    }
  }

  // 5. Strict mode prefix
  if (scenario === 'strict') {
    parts.push('"use strict";\n');
  }

  // 6. Test source
  parts.push(testSource);

  return {
    testPath,
    scenario,
    source: parts.join('\n'),
    metadata,
  };
}
