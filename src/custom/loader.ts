import type { TestTreeNode, TestEntry } from '../types/test262';

const sourceFiles = import.meta.glob('./tests/**/*.ts', { query: '?raw', import: 'default', eager: true }) as Record<string, string>;
const expectedFiles = import.meta.glob('./tests/**/*.expected', { query: '?raw', import: 'default', eager: true }) as Record<string, string>;

export interface CustomTest {
  name: string;
  relativePath: string;
  source: string;
  expected: string;
}

export interface CustomTestSuite {
  tree: TestTreeNode;
  tests: CustomTest[];
}

export function loadEmbeddedTests(): CustomTestSuite {
  const tests: CustomTest[] = [];

  for (const [srcPath, source] of Object.entries(sourceFiles)) {
    // srcPath looks like './tests/basic/hello.ts'
    const expectedPath = srcPath.replace(/\.ts$/, '.expected');
    const expected = expectedFiles[expectedPath];
    if (expected === undefined) continue;

    // relativePath: 'basic/hello.ts'
    const relativePath = srcPath.replace(/^\.\/tests\//, '');
    const name = relativePath.replace(/\.ts$/, '').split('/').pop()!;

    tests.push({ name, relativePath, source, expected });
  }

  const tree = buildTree(tests);
  return { tree, tests };
}

function buildTree(tests: CustomTest[]): TestTreeNode {
  const root: TestTreeNode = {
    name: 'custom tests',
    path: 'test',
    children: new Map(),
    tests: [],
  };

  for (const test of tests) {
    const parts = test.relativePath.split('/');
    parts.pop(); // remove filename
    let node = root;

    for (const part of parts) {
      if (!node.children.has(part)) {
        node.children.set(part, {
          name: part,
          path: node.path + '/' + part,
          children: new Map(),
          tests: [],
        });
      }
      node = node.children.get(part)!;
    }

    const entry: TestEntry = {
      path: 'test/' + test.relativePath,
      relativePath: test.relativePath,
    };
    node.tests.push(entry);
  }

  return root;
}
