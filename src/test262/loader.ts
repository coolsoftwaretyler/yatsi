import JSZip from 'jszip';
import type { TestSuiteBundle, TestTreeNode, TestEntry } from '../types/test262';

export type ProgressCallback = (loaded: number, total: number) => void;

export async function loadFromFile(file: File): Promise<TestSuiteBundle> {
  const data = await file.arrayBuffer();
  const zip = await JSZip.loadAsync(data);
  return buildBundle(zip);
}

export async function loadFromUrl(
  url: string,
  onProgress?: ProgressCallback,
): Promise<TestSuiteBundle> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to fetch: ${response.status} ${response.statusText}`);
  }

  const contentLength = Number(response.headers.get('Content-Length') || 0);
  const reader = response.body!.getReader();
  const chunks: Uint8Array[] = [];
  let loaded = 0;

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    loaded += value.length;
    if (onProgress && contentLength > 0) {
      onProgress(loaded, contentLength);
    }
  }

  const blob = new Blob(chunks as BlobPart[]);
  const data = await blob.arrayBuffer();
  const zip = await JSZip.loadAsync(data);
  return buildBundle(zip);
}

async function buildBundle(zip: JSZip): Promise<TestSuiteBundle> {
  const prefix = findRootPrefix(zip);

  const testDir = prefix + 'test/';
  const harnessDir = prefix + 'harness/';

  const hasTests = Object.keys(zip.files).some(p => p.startsWith(testDir));
  const hasHarness = Object.keys(zip.files).some(p => p.startsWith(harnessDir));

  if (!hasTests) {
    throw new Error('Invalid test262 archive: missing test/ directory');
  }
  if (!hasHarness) {
    throw new Error('Invalid test262 archive: missing harness/ directory');
  }

  // Eagerly load all harness files
  const harness = new Map<string, string>();
  const harnessPromises: Promise<void>[] = [];

  zip.forEach((_, file) => {
    const fullPath = file.name;
    if (fullPath.startsWith(harnessDir) && !file.dir && fullPath.endsWith('.js')) {
      const name = fullPath.slice(harnessDir.length);
      harnessPromises.push(
        file.async('string').then(content => {
          harness.set(name, content);
        }),
      );
    }
  });

  await Promise.all(harnessPromises);

  // Build test tree
  const root: TestTreeNode = { name: 'test', path: 'test', children: new Map(), tests: [] };
  let testCount = 0;

  zip.forEach((_, file) => {
    const fullPath = file.name;
    if (!fullPath.startsWith(testDir) || file.dir || !fullPath.endsWith('.js')) return;
    if (fullPath.includes('_FIXTURE')) return;

    const relativePath = fullPath.slice(testDir.length);
    const entry: TestEntry = { path: fullPath, relativePath };
    insertIntoTree(root, relativePath, entry);
    testCount++;
  });

  const getFileContent = (path: string): Promise<string> => {
    const file = zip.file(path);
    if (!file) {
      throw new Error(`File not found in archive: ${path}`);
    }
    return file.async('string');
  };

  return { tree: root, harness, getFileContent, testCount };
}

function findRootPrefix(zip: JSZip): string {
  const paths = Object.keys(zip.files);

  if (paths.some(p => p.startsWith('test/'))) {
    return '';
  }

  const topDirs = new Set<string>();
  for (const p of paths) {
    const firstSlash = p.indexOf('/');
    if (firstSlash > 0) {
      topDirs.add(p.slice(0, firstSlash + 1));
    }
  }

  for (const dir of topDirs) {
    if (paths.some(p => p.startsWith(dir + 'test/'))) {
      return dir;
    }
  }

  return '';
}

function insertIntoTree(root: TestTreeNode, relativePath: string, entry: TestEntry) {
  const segments = relativePath.split('/');
  let current = root;

  for (let i = 0; i < segments.length - 1; i++) {
    const seg = segments[i];
    let child = current.children.get(seg);
    if (!child) {
      const childPath = current.path + '/' + seg;
      child = { name: seg, path: childPath, children: new Map(), tests: [] };
      current.children.set(seg, child);
    }
    current = child;
  }

  current.tests.push(entry);
}
