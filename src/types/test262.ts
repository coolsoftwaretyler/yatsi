export interface TestMetadata {
  description: string;
  features: string[];
  flags: string[];
  includes: string[];
  negative: { phase: string; type: string } | null;
  locale: string[];
}

export interface TestTreeNode {
  name: string;
  path: string;
  children: Map<string, TestTreeNode>;
  tests: TestEntry[];
}

export interface TestEntry {
  path: string;
  relativePath: string;
}

export type TestOutcome = 'pass' | 'fail' | 'skip' | 'timeout';

export type TestScenario = 'strict' | 'non-strict' | 'raw' | 'module';

export interface TestResult {
  testPath: string;
  scenario: TestScenario;
  outcome: TestOutcome;
  errorType: string;
  errorMessage: string;
  duration: number;
}

export interface AssembledTest {
  testPath: string;
  scenario: TestScenario;
  source: string;
  metadata: TestMetadata;
}

export interface TestSuiteBundle {
  tree: TestTreeNode;
  harness: Map<string, string>;
  getFileContent: (path: string) => Promise<string>;
  testCount: number;
}

export interface TestCounts {
  pass: number;
  fail: number;
  skip: number;
  timeout: number;
  total: number;
}
