import { el, siteFooter } from './ui/components';
import { YatsiEngine } from './engine/wasm-engine';

// --- Types ---

interface Token {
  kind: string;
  lexeme: string;
  line: number;
  column: number;
  start?: number;
  end?: number;
}

interface ScopeBinding {
  name: string;
  type: string;
}

interface TypeCheckerStep {
  type: string;
  depth: number;
  nodeType: string;
  desc: string;
  variableName?: string;
  typeResult?: string;
  expectedType?: string;
  actualType?: string;
  operatorKind?: string;
  line?: number;
  col?: number;
  scopeLabel?: string;
  scopeBindings?: ScopeBinding[];
}

interface TypeCheckerTraceResult {
  steps: TypeCheckerStep[];
  tokens: Token[];
  ast: string;
  warnings: string[];
  errors: string[];
}

// --- Examples ---

const EXAMPLES: { label: string; code: string }[] = [
  {
    label: 'Basic types',
    code: 'let x = 10;\nlet y = "hello";\nlet z = true;',
  },
  {
    label: 'Type mismatch',
    code: 'let x: string = 42;\nlet y: number = "hello";',
  },
  {
    label: 'Binary operators',
    code: 'let a = 10;\nlet b = 20;\nlet sum = a + b;\nlet gt = a > b;',
  },
  {
    label: 'Function types',
    code: 'function add(a: number, b: number): number {\n  return a + b;\n}\nlet result = add(1, 2);',
  },
  {
    label: 'Scopes',
    code: 'let x = 1;\n{\n  let y = 2;\n  let z = x + y;\n}',
  },
  {
    label: 'Return type check',
    code: 'function greet(name: string): string {\n  return 42;\n}',
  },
];

// --- State ---

let engine: YatsiEngine | null = null;
let debounceTimer: ReturnType<typeof setTimeout> | null = null;

// Step mode state
let stepMode = false;
let stepData: TypeCheckerTraceResult | null = null;
let stepIndex = 0;
let stepSource = '';
let autoPlayTimer: ReturnType<typeof setTimeout> | null = null;

// --- DOM refs ---

let editor: HTMLTextAreaElement;
let editorDisplay: HTMLPreElement;
let stepToggleBtn: HTMLButtonElement;
let exampleSelect: HTMLSelectElement;
let stepControlsEl: HTMLElement;
let stepResetBtn: HTMLButtonElement;
let stepPrevBtn: HTMLButtonElement;
let stepNextBtn: HTMLButtonElement;
let stepAutoBtn: HTMLButtonElement;
let stepExitBtn: HTMLButtonElement;
let stepCounterEl: HTMLElement;
let stepInfoEl: HTMLElement;
let astPanel: HTMLElement;
let scopePanel: HTMLElement;
let warningsPanel: HTMLElement;

// --- Helpers ---

function escapeHtml(str: string): string {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

// --- Static rendering (non-step mode) ---

function runStatic(): void {
  if (!engine) return;
  if (stepMode) return;

  const source = editor.value;
  if (source.trim() === '') {
    astPanel.textContent = '';
    scopePanel.textContent = '';
    warningsPanel.innerHTML = '';
    return;
  }

  try {
    const astResult = engine.dumpTypedAst(source);
    astPanel.textContent = astResult.consoleOutput || astResult.errorMessage || '';

    const tcResult = engine.typecheck(source);
    if (tcResult.consoleOutput) {
      warningsPanel.innerHTML = tcResult.consoleOutput
        .split('\n')
        .filter((l) => l.trim())
        .map((w) => '<div class="te-warning-item">' + escapeHtml(w) + '</div>')
        .join('');
    } else {
      warningsPanel.innerHTML =
        '<span class="te-no-warnings">No warnings</span>';
    }

    scopePanel.innerHTML =
      '<span class="te-scope-hint">Enter step mode to see scope state</span>';
  } catch (e) {
    astPanel.innerHTML =
      '<div class="error">' +
      escapeHtml((e as Error).message) +
      '</div>';
  }
}

function debouncedRun(): void {
  if (stepMode) return;
  if (debounceTimer) clearTimeout(debounceTimer);
  debounceTimer = setTimeout(runStatic, 300);
}

// --- Step mode ---

function enterStepMode(): void {
  if (!engine) return;

  stepSource = editor.value;
  if (stepSource.trim() === '') return;

  try {
    const json = engine.typecheckTraced(stepSource);
    stepData = JSON.parse(json);
  } catch (e) {
    warningsPanel.innerHTML =
      '<div class="error">Trace error: ' +
      escapeHtml((e as Error).message) +
      '</div>';
    return;
  }

  if (!stepData || !stepData.steps || stepData.steps.length === 0) return;

  if (stepData.errors && stepData.errors.length > 0) {
    warningsPanel.innerHTML = stepData.errors
      .map((e) => '<div class="error">' + escapeHtml(e) + '</div>')
      .join('');
    return;
  }

  stepMode = true;
  stepIndex = 0;

  editor.classList.add('hidden');
  editorDisplay.classList.remove('hidden');

  stepControlsEl.classList.remove('hidden');
  stepInfoEl.classList.remove('hidden');
  stepToggleBtn.classList.add('active');
  stepToggleBtn.textContent = 'Exit Step';

  renderStepState();
}

function exitStepMode(): void {
  stopAutoPlay();
  stepMode = false;
  stepData = null;
  stepIndex = 0;
  stepSource = '';

  editorDisplay.classList.add('hidden');
  editor.classList.remove('hidden');

  stepControlsEl.classList.add('hidden');
  stepInfoEl.classList.add('hidden');
  stepToggleBtn.classList.remove('active');
  stepToggleBtn.textContent = 'Step';

  runStatic();
}

function stepNext(): void {
  if (!stepMode || !stepData) return;
  if (stepIndex < stepData.steps.length - 1) {
    stepIndex++;
    renderStepState();
  } else {
    stopAutoPlay();
  }
}

function stepPrev(): void {
  if (!stepMode || !stepData) return;
  if (stepIndex > 0) {
    stepIndex--;
    renderStepState();
  }
}

function stepReset(): void {
  if (!stepMode || !stepData) return;
  stopAutoPlay();
  stepIndex = 0;
  renderStepState();
}

// --- Auto-play ---

function toggleAutoPlay(): void {
  if (autoPlayTimer) {
    stopAutoPlay();
  } else {
    startAutoPlay();
  }
}

function startAutoPlay(): void {
  if (!stepMode || !stepData) return;
  if (stepIndex >= stepData.steps.length - 1) {
    stepIndex = 0;
    renderStepState();
  }
  stepAutoBtn.innerHTML = '&#9646;&#9646;';
  stepAutoBtn.classList.add('active');
  autoPlayTick();
}

function autoPlayTick(): void {
  if (!stepMode || !stepData) {
    stopAutoPlay();
    return;
  }
  if (stepIndex >= stepData.steps.length - 1) {
    stopAutoPlay();
    return;
  }

  stepIndex++;
  renderStepState();

  const step = stepData.steps[stepIndex];
  const delay = getAutoPlayDelay(step);
  autoPlayTimer = setTimeout(autoPlayTick, delay);
}

function getAutoPlayDelay(step: TypeCheckerStep): number {
  switch (step.type) {
    case 'enterStmt':
    case 'exitStmt':
    case 'enterExpr':
    case 'exitExpr':
      return 150;
    case 'defineVariable':
    case 'defineParam':
      return 400;
    case 'lookupVariable':
    case 'lookupVariableFail':
      return 300;
    case 'checkBinaryOp':
    case 'checkUnaryOp':
      return 350;
    case 'inferFromLiteral':
    case 'inferFromInitializer':
      return 350;
    case 'resolveAnnotation':
      return 300;
    case 'enterScope':
    case 'exitScope':
    case 'enterFunction':
    case 'exitFunction':
      return 250;
    case 'warning':
      return 600;
    case 'setReturnType':
    case 'checkReturnValue':
    case 'checkCallArgs':
      return 350;
    default:
      return 300;
  }
}

function stopAutoPlay(): void {
  if (autoPlayTimer) {
    clearTimeout(autoPlayTimer);
    autoPlayTimer = null;
  }
  stepAutoBtn.innerHTML = '&#9654;&#9654;';
  stepAutoBtn.classList.remove('active');
}

// --- Step rendering ---

function renderStepState(): void {
  if (!stepData) return;
  const step = stepData.steps[stepIndex];

  stepCounterEl.textContent =
    stepIndex + 1 + ' / ' + stepData.steps.length;

  renderStepInfo(step);
  renderSourceHighlight(step);
  renderIncrementalAST();
  renderScopePanel();
  renderWarningsPanel();
}

function stepBadgeClass(type: string): string {
  switch (type) {
    case 'enterExpr':
    case 'exitExpr':
      return 'te-badge-expr';
    case 'enterStmt':
    case 'exitStmt':
      return 'te-badge-stmt';
    case 'defineVariable':
    case 'defineParam':
      return 'te-badge-define';
    case 'lookupVariable':
      return 'te-badge-lookup';
    case 'lookupVariableFail':
      return 'te-badge-lookup-fail';
    case 'checkBinaryOp':
    case 'checkUnaryOp':
      return 'te-badge-op';
    case 'inferFromLiteral':
    case 'inferFromInitializer':
      return 'te-badge-infer';
    case 'resolveAnnotation':
      return 'te-badge-resolve';
    case 'enterScope':
    case 'exitScope':
      return 'te-badge-scope';
    case 'enterFunction':
    case 'exitFunction':
      return 'te-badge-function';
    case 'setReturnType':
    case 'checkReturnValue':
    case 'checkCallArgs':
      return 'te-badge-check';
    case 'warning':
      return 'te-badge-warning';
    default:
      return 'te-badge-default';
  }
}

function stepLabel(type: string): string {
  switch (type) {
    case 'enterExpr':
      return 'enter expr';
    case 'exitExpr':
      return 'exit expr';
    case 'enterStmt':
      return 'enter stmt';
    case 'exitStmt':
      return 'exit stmt';
    case 'defineVariable':
      return 'define';
    case 'defineParam':
      return 'param';
    case 'lookupVariable':
      return 'lookup';
    case 'lookupVariableFail':
      return 'lookup fail';
    case 'checkBinaryOp':
      return 'binary op';
    case 'checkUnaryOp':
      return 'unary op';
    case 'inferFromLiteral':
      return 'literal';
    case 'inferFromInitializer':
      return 'infer';
    case 'resolveAnnotation':
      return 'annotation';
    case 'enterScope':
      return 'enter scope';
    case 'exitScope':
      return 'exit scope';
    case 'enterFunction':
      return 'enter fn';
    case 'exitFunction':
      return 'exit fn';
    case 'setReturnType':
      return 'return type';
    case 'checkReturnValue':
      return 'check return';
    case 'checkCallArgs':
      return 'check args';
    case 'warning':
      return 'warning';
    default:
      return type;
  }
}

function renderStepInfo(step: TypeCheckerStep): void {
  const badgeCls = 'te-step-badge ' + stepBadgeClass(step.type);
  const label = stepLabel(step.type);

  let typeResultHtml = '';
  if (step.typeResult) {
    typeResultHtml =
      ' <span class="te-type-pill te-type-' +
      step.typeResult +
      '">' +
      escapeHtml(step.typeResult) +
      '</span>';
  }

  const nodeHtml = step.nodeType
    ? '<span class="te-step-node">' + escapeHtml(step.nodeType) + '</span> '
    : '';

  stepInfoEl.innerHTML =
    '<span class="' +
    badgeCls +
    '">' +
    escapeHtml(label) +
    '</span> ' +
    nodeHtml +
    '<span class="te-step-desc">' +
    escapeHtml(step.desc) +
    '</span>' +
    typeResultHtml;
}

function renderSourceHighlight(step: TypeCheckerStep): void {
  const tokens = stepData!.tokens;
  if (!tokens || tokens.length === 0) {
    editorDisplay.textContent = stepSource;
    return;
  }

  const line = step.line;
  const col = step.col;

  if (line === undefined || line < 0) {
    // No source location — find one from parents via the step's node context
    editorDisplay.textContent = stepSource;
    return;
  }

  // Find the matching token
  let matchToken: Token | null = null;
  for (const tok of tokens) {
    if (tok.line === line && tok.column === col) {
      matchToken = tok;
      break;
    }
  }

  if (!matchToken) {
    // Fallback: find token closest to line
    for (const tok of tokens) {
      if (tok.line === line) {
        matchToken = tok;
        break;
      }
    }
  }

  if (!matchToken || matchToken.start === undefined) {
    editorDisplay.textContent = stepSource;
    return;
  }

  const start = matchToken.start;
  const end = matchToken.end || start;

  let html = '';
  if (start > 0) {
    html += escapeHtml(stepSource.substring(0, start));
  }
  if (end > start) {
    html +=
      '<mark class="te-highlight">' +
      escapeHtml(stepSource.substring(start, end)) +
      '</mark>';
  }
  if (end < stepSource.length) {
    html += escapeHtml(stepSource.substring(end));
  }

  editorDisplay.innerHTML = html;
}

function renderIncrementalAST(): void {
  if (!stepData) return;

  const astText = stepData.ast;
  if (!astText) {
    astPanel.innerHTML = '<span class="te-ast-empty">No AST</span>';
    return;
  }

  const lines = astText.split('\n');
  const step = stepData.steps[stepIndex];

  // Build a set of resolved types from steps up to current
  const resolvedTypes: Map<string, string> = new Map();
  for (let i = 0; i <= stepIndex; i++) {
    const s = stepData.steps[i];
    if (
      s.type === 'exitExpr' &&
      s.typeResult &&
      s.nodeType
    ) {
      resolvedTypes.set(
        s.nodeType + ':' + (s.line || '') + ':' + (s.col || ''),
        s.typeResult
      );
    }
  }

  // Determine "current node" from the step
  let currentLine = step.line;
  let currentNodeType = step.nodeType;

  let html = '<div class="te-ast-tree">';
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line.trim() === '') continue;

    const trimmed = line.trimStart();
    const indent = line.substring(0, line.length - trimmed.length);

    // Check if this AST line corresponds to the current step
    const nodeMatch = trimmed.match(/^([A-Z][A-Za-z]*)/);
    let isActive = false;
    if (
      nodeMatch &&
      currentNodeType &&
      nodeMatch[1] === currentNodeType &&
      currentLine !== undefined
    ) {
      // Simple heuristic: match on node type
      isActive = true;
      // Only highlight the first match
      currentNodeType = '';
    }

    // Check if this line has a type annotation already from the AST dump
    const typeMatch = trimmed.match(/:: (\w+)/);
    let typeBadge = '';
    if (typeMatch) {
      typeBadge =
        ' <span class="te-type-pill te-type-' +
        typeMatch[1] +
        '">' +
        escapeHtml(typeMatch[1]) +
        '</span>';
    }

    let cls = 'te-ast-line';
    if (isActive) cls += ' te-ast-active';

    html +=
      '<div class="' +
      cls +
      '"><span class="te-ast-indent">' +
      escapeHtml(indent) +
      '</span>' +
      escapeHtml(trimmed) +
      typeBadge +
      '</div>';
  }
  html += '</div>';
  astPanel.innerHTML = html;

  const activeNode = astPanel.querySelector('.te-ast-active');
  if (activeNode) {
    activeNode.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  }
}

function renderScopePanel(): void {
  if (!stepData) return;

  // Replay scope state up to current step
  interface ScopeFrame {
    label: string;
    bindings: Map<string, string>;
  }

  const scopeStack: ScopeFrame[] = [{ label: 'global', bindings: new Map() }];
  let newestVar = '';

  for (let i = 0; i <= stepIndex; i++) {
    const s = stepData.steps[i];

    if (s.type === 'enterScope' || s.type === 'enterFunction') {
      scopeStack.push({
        label: s.scopeLabel || s.desc || 'block',
        bindings: new Map(),
      });
    } else if (s.type === 'exitScope' || s.type === 'exitFunction') {
      if (scopeStack.length > 1) scopeStack.pop();
    } else if (
      s.type === 'defineVariable' ||
      s.type === 'defineParam'
    ) {
      if (s.variableName && s.typeResult) {
        const top = scopeStack[scopeStack.length - 1];
        top.bindings.set(s.variableName, s.typeResult);
        if (i === stepIndex) newestVar = s.variableName;
      }
    }
  }

  let html = '<div class="te-scope-chain">';

  // Show from innermost to outermost
  for (let i = scopeStack.length - 1; i >= 0; i--) {
    const frame = scopeStack[i];
    const isCurrent = i === scopeStack.length - 1;
    const frameCls =
      'te-scope-card' + (isCurrent ? ' te-scope-card-current' : '');

    html +=
      '<div class="' +
      frameCls +
      '"><div class="te-scope-card-header">' +
      escapeHtml(frame.label) +
      '</div>';

    if (frame.bindings.size === 0) {
      html += '<div class="te-scope-empty">(empty)</div>';
    } else {
      html += '<div class="te-scope-bindings">';
      for (const [name, type] of frame.bindings) {
        const isNew = name === newestVar && isCurrent;
        const entryCls =
          'te-scope-entry' + (isNew ? ' te-scope-entry-new' : '');
        html +=
          '<div class="' +
          entryCls +
          '"><span class="te-scope-name">' +
          escapeHtml(name) +
          '</span>: <span class="te-type-pill te-type-' +
          type +
          '">' +
          escapeHtml(type) +
          '</span></div>';
      }
      html += '</div>';
    }

    html += '</div>';

    if (i > 0) {
      html += '<div class="te-scope-arrow">\u25B4</div>';
    }
  }

  html += '</div>';
  scopePanel.innerHTML = html;
}

function renderWarningsPanel(): void {
  if (!stepData) return;

  // Collect warnings up to current step
  const warnings: string[] = [];
  for (let i = 0; i <= stepIndex; i++) {
    if (stepData.steps[i].type === 'warning') {
      warnings.push(stepData.steps[i].desc);
    }
  }

  if (warnings.length === 0) {
    warningsPanel.innerHTML =
      '<span class="te-no-warnings">No warnings yet</span>';
    return;
  }

  warningsPanel.innerHTML = warnings
    .map(
      (w, i) =>
        '<div class="te-warning-item' +
        (i === warnings.length - 1 ? ' te-warning-new' : '') +
        '">' +
        escapeHtml(w) +
        '</div>'
    )
    .join('');
}

// --- Build DOM ---

function buildPage(): void {
  const root = document.getElementById('app')!;

  // Nav
  const nav = el(
    'nav',
    { className: 'app-nav' },
    el('a', { href: '../' }, 'Home'),
    el('a', { href: '../playground/' }, 'Playground'),
    el('a', { href: '../custom/' }, 'Custom Tests'),
    el('a', { href: '../test262/' }, 'Test262 Runner')
  );

  const header = el(
    'header',
    { className: 'app-header' },
    el('h1', {}, 'Yatsi'),
    el('p', { className: 'subtitle' }, 'Type Explorer'),
    nav
  );

  // Example select
  exampleSelect = document.createElement('select');
  exampleSelect.className = 'te-example-select';
  const defaultOpt = el('option', { value: '' }, 'Examples...');
  exampleSelect.appendChild(defaultOpt);
  for (let i = 0; i < EXAMPLES.length; i++) {
    const opt = el('option', { value: String(i) }, EXAMPLES[i].label);
    exampleSelect.appendChild(opt);
  }

  // Step toggle
  stepToggleBtn = el('button', { className: 'step-toggle' }, 'Step');

  // Toolbar
  const toolbar = el(
    'div',
    { className: 'te-toolbar' },
    exampleSelect,
    stepToggleBtn
  );

  // Editor
  editor = document.createElement('textarea');
  editor.id = 'editor';
  editor.className = 'te-editor';
  editor.spellcheck = false;
  editor.setAttribute('autocorrect', 'off');
  editor.setAttribute('autocapitalize', 'off');
  editor.placeholder = 'Type TypeScript here...';

  editorDisplay = el('pre', { className: 'te-editor-display hidden' });

  const editorPane = el(
    'div',
    { className: 'te-editor-pane' },
    el('label', {}, 'Source'),
    editor,
    editorDisplay
  );

  // Step controls
  stepResetBtn = el('button', {}, '\u23EE');
  stepPrevBtn = el('button', {}, '\u25C0');
  stepNextBtn = el('button', {}, '\u25B6');
  stepAutoBtn = el('button', { className: 'step-auto-btn' }, '\u25B6\u25B6');
  stepExitBtn = el('button', {}, 'Exit');
  stepCounterEl = el('span', { className: 'te-step-counter' });

  stepControlsEl = el(
    'div',
    { className: 'te-step-controls hidden' },
    stepResetBtn,
    stepPrevBtn,
    stepNextBtn,
    stepAutoBtn,
    stepExitBtn,
    stepCounterEl
  );

  stepInfoEl = el('div', { className: 'te-step-info hidden' });

  // Output panels
  astPanel = el('div', { className: 'te-panel te-ast-panel' });
  scopePanel = el('div', { className: 'te-panel te-scope-panel' });
  warningsPanel = el('div', { className: 'te-panel te-warnings-panel' });

  const outputColumn = el(
    'div',
    { className: 'te-output-col' },
    el('div', { className: 'te-panel-label' }, 'Typed AST'),
    astPanel,
    el('div', { className: 'te-panel-label' }, 'Scope'),
    scopePanel,
    el('div', { className: 'te-panel-label' }, 'Warnings'),
    warningsPanel
  );

  // Main layout
  const mainLayout = el(
    'div',
    { className: 'type-explorer' },
    toolbar,
    stepControlsEl,
    stepInfoEl,
    el('div', { className: 'te-main' }, editorPane, outputColumn)
  );

  root.appendChild(header);
  root.appendChild(mainLayout);
  root.appendChild(siteFooter());

  // --- Wire up events ---

  editor.addEventListener('input', debouncedRun);

  editor.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.key === 'Tab') {
      e.preventDefault();
      const start = editor.selectionStart;
      const end = editor.selectionEnd;
      editor.value =
        editor.value.substring(0, start) + '  ' + editor.value.substring(end);
      editor.selectionStart = editor.selectionEnd = start + 2;
      debouncedRun();
    }
  });

  exampleSelect.addEventListener('change', () => {
    const idx = Number(exampleSelect.value);
    if (!isNaN(idx) && EXAMPLES[idx]) {
      if (stepMode) exitStepMode();
      editor.value = EXAMPLES[idx].code;
      runStatic();
      exampleSelect.value = '';
    }
  });

  stepToggleBtn.addEventListener('click', () => {
    if (stepMode) exitStepMode();
    else enterStepMode();
  });

  stepNextBtn.addEventListener('click', stepNext);
  stepPrevBtn.addEventListener('click', stepPrev);
  stepResetBtn.addEventListener('click', stepReset);
  stepExitBtn.addEventListener('click', exitStepMode);
  stepAutoBtn.addEventListener('click', toggleAutoPlay);

  document.addEventListener('keydown', (e: KeyboardEvent) => {
    if (!stepMode) return;
    if (e.target === editor) return;

    switch (e.key) {
      case 'ArrowRight':
      case 'Enter':
        e.preventDefault();
        stepNext();
        break;
      case 'ArrowLeft':
        e.preventDefault();
        stepPrev();
        break;
      case 'Escape':
        e.preventDefault();
        exitStepMode();
        break;
      case 'Home':
        e.preventDefault();
        stepReset();
        break;
      case ' ':
        e.preventDefault();
        toggleAutoPlay();
        break;
    }
  });
}

// --- Init ---

async function init(): Promise<void> {
  buildPage();
  engine = await YatsiEngine.create();

  // Load first example by default
  editor.value = EXAMPLES[0].code;
  runStatic();
}

init();
