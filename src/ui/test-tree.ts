import { el, badge, clearChildren } from './components';
import type { TestTreeNode, TestResult, TestCounts } from '../types/test262';

export class TestTree {
  readonly element: HTMLElement;
  private nodeCounts = new Map<string, TestCounts>();
  private nodeElements = new Map<string, HTMLElement>();

  constructor() {
    this.element = el('div', { className: 'test-tree' });
  }

  render(root: TestTreeNode): void {
    clearChildren(this.element);
    this.nodeCounts.clear();
    this.nodeElements.clear();
    this.element.appendChild(this.renderNode(root, 0));
  }

  updateResult(result: TestResult): void {
    // Update counts along the path
    const parts = result.testPath.split('/');
    let path = 'test';

    for (let i = 0; i < parts.length; i++) {
      this.incrementCount(path, result.outcome);
      if (i < parts.length - 1) {
        path += '/' + parts[i];
      }
    }

    // Re-render badges for affected nodes
    for (const [nodePath, elem] of this.nodeElements) {
      const counts = this.nodeCounts.get(nodePath);
      if (counts) {
        const badgeContainer = elem.querySelector('.tree-badges');
        if (badgeContainer) {
          clearChildren(badgeContainer as HTMLElement);
          this.appendBadges(badgeContainer as HTMLElement, counts);
        }
      }
    }
  }

  private incrementCount(path: string, outcome: string): void {
    let counts = this.nodeCounts.get(path);
    if (!counts) {
      counts = { pass: 0, fail: 0, skip: 0, timeout: 0, total: 0 };
      this.nodeCounts.set(path, counts);
    }
    counts.total++;
    if (outcome === 'pass') counts.pass++;
    else if (outcome === 'fail') counts.fail++;
    else if (outcome === 'skip') counts.skip++;
    else if (outcome === 'timeout') counts.timeout++;
  }

  private renderNode(node: TestTreeNode, depth: number): HTMLElement {
    const container = el('div', { className: 'tree-node' });
    this.nodeElements.set(node.path, container);

    const header = el('div', { className: 'tree-header' });
    header.style.paddingLeft = `${depth * 16}px`;

    const hasChildren = node.children.size > 0 || node.tests.length > 0;
    const toggle = el('span', { className: 'tree-toggle' }, hasChildren ? '\u25B6' : ' ');
    const label = el('span', { className: 'tree-label' }, node.name);
    const badges = el('span', { className: 'tree-badges' });

    header.appendChild(toggle);
    header.appendChild(label);
    header.appendChild(badges);
    container.appendChild(header);

    const childContainer = el('div', { className: 'tree-children hidden' });

    // Sort children alphabetically
    const sortedChildren = [...node.children.entries()].sort((a, b) => a[0].localeCompare(b[0]));
    for (const [, child] of sortedChildren) {
      childContainer.appendChild(this.renderNode(child, depth + 1));
    }

    // Render test files
    for (const test of node.tests) {
      const fileName = test.relativePath.split('/').pop() || test.relativePath;
      const testEl = el('div', { className: 'tree-leaf' });
      testEl.style.paddingLeft = `${(depth + 1) * 16}px`;
      testEl.textContent = fileName;
      childContainer.appendChild(testEl);
    }

    container.appendChild(childContainer);

    if (hasChildren) {
      header.addEventListener('click', () => {
        const isHidden = childContainer.classList.contains('hidden');
        childContainer.classList.toggle('hidden');
        toggle.textContent = isHidden ? '\u25BC' : '\u25B6';
      });
      header.style.cursor = 'pointer';
    }

    return container;
  }

  private appendBadges(container: HTMLElement, counts: TestCounts): void {
    if (counts.pass > 0) {
      container.appendChild(badge(String(counts.pass), 'badge-pass'));
    }
    if (counts.fail > 0) {
      container.appendChild(badge(String(counts.fail), 'badge-fail'));
    }
    if (counts.skip > 0) {
      container.appendChild(badge(String(counts.skip), 'badge-skip'));
    }
    if (counts.timeout > 0) {
      container.appendChild(badge(String(counts.timeout), 'badge-timeout'));
    }
  }
}
