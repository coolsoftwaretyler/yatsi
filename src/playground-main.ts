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

interface LexerStep {
  type: string;
  pos: number;
  endPos: number;
  ch?: string;
  expected?: string;
  tokenIndex?: number;
  kind?: string;
  lexeme?: string;
  desc: string;
}

interface ParserStep {
  type: string;
  tokenPos: number;
  depth: number;
  rule: string;
  desc: string;
}

interface PipelineResult {
  ok: boolean;
  tokens: Token[];
  ast?: string;
  bytecode?: string;
  output?: string;
  errors?: string[];
  runtimeError?: boolean;
}

interface LexerTraceResult {
  steps: LexerStep[];
  tokens: Token[];
}

interface ParserTraceResult {
  steps: ParserStep[];
  tokens: Token[];
  ast: string;
  errors: string[];
}

interface CompilerStep {
  type: string;
  depth: number;
  nodeType: string;
  desc: string;
  instrIndex?: number;
  registerId?: number;
  constantIndex?: number;
  patchTarget?: number;
}

interface CompilerInstruction {
  opcode: string;
  a: number;
  b: number;
  c: number;
  bx: number;
  sbx: number;
}

interface CompilerTraceResult {
  steps: CompilerStep[];
  instructions: CompilerInstruction[];
  constants: string[];
  registerCount: number;
  ast: string;
  bytecode: string;
  errors: string[];
}

// --- State ---

let engine: YatsiEngine | null = null;
let debounceTimer: ReturnType<typeof setTimeout> | null = null;

// Step mode state
let stepMode = false;
let stepModeType: 'lexer' | 'parser' | 'compiler' = 'lexer';
let stepData: LexerTraceResult | ParserTraceResult | CompilerTraceResult | null = null;
let stepIndex = 0;
let stepSource = '';
let autoPlayTimer: ReturnType<typeof setTimeout> | null = null;
let parserCallStack: string[] = [];
let compilerNodeStack: string[] = [];

// --- DOM refs ---

let editor: HTMLTextAreaElement;
let editorDisplay: HTMLPreElement;
let stepToggleBtn: HTMLButtonElement;
let stepModeSelect: HTMLSelectElement;
let stepControlsEl: HTMLElement;
let stepResetBtn: HTMLButtonElement;
let stepPrevBtn: HTMLButtonElement;
let stepNextBtn: HTMLButtonElement;
let stepAutoBtn: HTMLButtonElement;
let stepExitBtn: HTMLButtonElement;
let stepCounterEl: HTMLElement;
let stepInfoEl: HTMLElement;
let outputPane: HTMLElement;
let panels: Record<string, HTMLElement>;
let tabs: HTMLButtonElement[];
let splitLabelTokens: HTMLElement;
let splitLabelAst: HTMLElement;
let splitLabelBytecode: HTMLElement;

// --- Helpers ---

function escapeHtml(str: string): string {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

// --- Tab switching ---

function switchTab(name: string): void {
  for (const btn of tabs) {
    btn.classList.toggle('active', btn.dataset.tab === name);
  }
  for (const key of Object.keys(panels)) {
    panels[key].classList.toggle('active', key === name);
  }
}

// --- Rendering ---

function renderTokens(tokens: Token[], upToIndex: number): void {
  if (!tokens || tokens.length === 0) {
    panels.tokens.textContent = 'No tokens';
    return;
  }

  const showAll = stepModeType === 'parser';

  let html =
    '<table class="token-table"><thead><tr>' +
    '<th>Loc</th><th>Kind</th><th>Lexeme</th>' +
    '</tr></thead><tbody>';

  for (let i = 0; i < tokens.length; i++) {
    const tok = tokens[i];
    let rowClass = '';
    if (stepMode && showAll) {
      if (upToIndex >= 0 && i === upToIndex) {
        rowClass = ' class="token-current-parse"';
      } else if (upToIndex >= 0 && i < upToIndex) {
        rowClass = ' class="token-consumed"';
      }
    } else if (stepMode && !showAll) {
      if (i > upToIndex) break;
      if (i === upToIndex) {
        rowClass = ' class="token-current"';
      }
    }
    html +=
      '<tr' + rowClass + '><td>' +
      tok.line + ':' + tok.column +
      '</td><td>' + escapeHtml(tok.kind) +
      '</td><td>' + escapeHtml(tok.lexeme) +
      '</td></tr>';
  }

  html += '</tbody></table>';
  panels.tokens.innerHTML = html;

  if (stepMode && upToIndex >= 0) {
    const selector = showAll ? '.token-current-parse' : '.token-current';
    const currentRow = panels.tokens.querySelector(selector);
    if (currentRow) {
      currentRow.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

function renderErrors(errors: string[]): string {
  let html = '';
  for (const err of errors) {
    html += '<div class="error">' + escapeHtml(err) + '</div>';
  }
  return html;
}

function renderResult(result: PipelineResult): void {
  renderTokens(result.tokens, -1);

  if (!result.ok) {
    const errHtml = renderErrors(result.errors || []);
    panels.ast.innerHTML = errHtml;
    panels.bytecode.innerHTML = errHtml;
    panels.output.innerHTML = errHtml;
    return;
  }

  panels.ast.textContent = result.ast || '';
  panels.bytecode.textContent = result.bytecode || '';

  if (result.runtimeError) {
    panels.output.innerHTML = '<div class="error">Runtime error</div>';
  } else {
    panels.output.textContent = result.output || '(no output)';
  }
}

// --- Pipeline execution ---

function runPipeline(): void {
  if (!engine) return;
  if (stepMode) return;

  const source = editor.value;
  if (source.trim() === '') {
    panels.tokens.textContent = '';
    panels.ast.textContent = '';
    panels.bytecode.textContent = '';
    panels.output.textContent = '';
    return;
  }

  try {
    const json = engine.runPipeline(source);
    const result: PipelineResult = JSON.parse(json);
    renderResult(result);
  } catch (e) {
    panels.output.innerHTML =
      '<div class="error">Internal error: ' + escapeHtml((e as Error).message) + '</div>';
  }
}

function debouncedRun(): void {
  if (stepMode) return;
  if (debounceTimer) clearTimeout(debounceTimer);
  debounceTimer = setTimeout(runPipeline, 300);
}

// --- Step mode ---

function enterStepMode(): void {
  if (!engine) return;

  stepSource = editor.value;
  if (stepSource.trim() === '') return;

  try {
    let json: string;
    if (stepModeType === 'compiler') {
      json = engine.compileTraced(stepSource);
    } else if (stepModeType === 'parser') {
      json = engine.parseTraced(stepSource);
    } else {
      json = engine.tokenizeTraced(stepSource);
    }
    stepData = JSON.parse(json);
  } catch (e) {
    panels.output.innerHTML =
      '<div class="error">Trace error: ' + escapeHtml((e as Error).message) + '</div>';
    return;
  }

  if (!stepData || !stepData.steps || stepData.steps.length === 0) return;

  // Check for parse errors in compiler mode
  if (stepModeType === 'compiler') {
    const cdata = stepData as CompilerTraceResult;
    if (cdata.errors && cdata.errors.length > 0) {
      panels.output.innerHTML = renderErrors(cdata.errors);
      return;
    }
  }

  stepMode = true;
  stepIndex = 0;
  parserCallStack = [];
  compilerNodeStack = [];

  editor.classList.add('hidden');
  editorDisplay.classList.remove('hidden');

  stepControlsEl.classList.remove('hidden');
  stepInfoEl.classList.remove('hidden');
  stepToggleBtn.classList.add('active');
  switchTab('tokens');

  if (stepModeType === 'parser') {
    outputPane.classList.add('parser-split');
    outputPane.classList.remove('compiler-split');
  } else if (stepModeType === 'compiler') {
    outputPane.classList.add('compiler-split');
    outputPane.classList.remove('parser-split');
  } else {
    outputPane.classList.remove('parser-split');
    outputPane.classList.remove('compiler-split');
  }

  panels.ast.textContent = '';
  panels.bytecode.textContent = '';
  panels.output.textContent = '';

  renderStepState();
}

function exitStepMode(): void {
  stopAutoPlay();
  stepMode = false;
  stepData = null;
  stepIndex = 0;
  stepSource = '';
  parserCallStack = [];

  editorDisplay.classList.add('hidden');
  editor.classList.remove('hidden');

  stepControlsEl.classList.add('hidden');
  stepInfoEl.classList.add('hidden');
  stepToggleBtn.classList.remove('active');
  outputPane.classList.remove('parser-split');
  outputPane.classList.remove('compiler-split');
  compilerNodeStack = [];

  runPipeline();
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
  if (!stepMode || !stepData) { stopAutoPlay(); return; }
  if (stepIndex >= stepData.steps.length - 1) { stopAutoPlay(); return; }

  stepIndex++;
  renderStepState();

  const step = stepData.steps[stepIndex];
  const delay = getAutoPlayDelay(step);
  autoPlayTimer = setTimeout(autoPlayTick, delay);
}

function getAutoPlayDelay(step: LexerStep | ParserStep | CompilerStep): number {
  if (stepModeType === 'compiler') {
    switch (step.type) {
      case 'enterNode':
      case 'exitNode':
        return 150;
      case 'allocRegister':
        return 200;
      case 'emitInstruction':
        return 400;
      case 'addConstant':
        return 350;
      case 'patchJump':
        return 500;
      case 'pushLoop':
      case 'popLoop':
        return 250;
      default:
        return 300;
    }
  } else if (stepModeType === 'parser') {
    switch (step.type) {
      case 'enterRule':
      case 'exitRule':
        return 150;
      case 'tryMatch':
        return 250;
      case 'consumeToken':
        return 400;
      case 'produceNode':
        return 400;
      case 'error':
        return 600;
      case 'synchronize':
        return 300;
      default:
        return 300;
    }
  } else {
    if (step.type === 'skipWs' || step.type === 'skipLineComment' || step.type === 'skipBlockComment') {
      return 150;
    } else if (step.type === 'scanStart') {
      return 300;
    } else if (step.type === 'advance' || step.type === 'match' || step.type === 'matchFail') {
      return 350;
    } else if (step.type === 'emit') {
      return 500;
    } else if (step.type === 'eof') {
      return 600;
    }
    return 400;
  }
}

function stopAutoPlay(): void {
  if (autoPlayTimer) {
    clearTimeout(autoPlayTimer);
    autoPlayTimer = null;
  }
  stepAutoBtn.innerHTML = '&#9654;';
  stepAutoBtn.classList.remove('active');
}

// --- Unified step rendering ---

function renderStepState(): void {
  if (!stepData) return;
  if (stepModeType === 'compiler') {
    renderCompilerStepState();
  } else if (stepModeType === 'parser') {
    renderParserStepState();
  } else {
    renderLexerStepState();
  }
}

// --- Lexer step rendering ---

function renderLexerStepState(): void {
  const data = stepData as LexerTraceResult;
  const steps = data.steps;
  const step = steps[stepIndex];

  stepCounterEl.textContent = (stepIndex + 1) + ' / ' + steps.length;
  renderLexerStepInfo(step);
  renderLexerSourceForStep(step);

  let lastEmittedTokenIndex = -1;
  for (let i = 0; i <= stepIndex; i++) {
    if (steps[i].type === 'emit' && steps[i].tokenIndex !== undefined && steps[i].tokenIndex! >= 0) {
      lastEmittedTokenIndex = steps[i].tokenIndex!;
    }
  }
  renderTokens(data.tokens, lastEmittedTokenIndex);
}

function renderLexerStepInfo(step: LexerStep): void {
  let badgeClass = 'step-badge';
  let label = step.type;
  switch (step.type) {
    case 'advance':          badgeClass += ' step-badge-advance'; label = 'advance'; break;
    case 'match':            badgeClass += ' step-badge-match'; label = 'match'; break;
    case 'matchFail':        badgeClass += ' step-badge-matchFail'; label = 'match fail'; break;
    case 'emit':             badgeClass += ' step-badge-emit'; label = 'emit'; break;
    case 'skipWs':           badgeClass += ' step-badge-skip'; label = 'skip ws'; break;
    case 'skipLineComment':  badgeClass += ' step-badge-skip'; label = 'skip //'; break;
    case 'skipBlockComment': badgeClass += ' step-badge-skip'; label = 'skip /**/'; break;
    case 'scanStart':        badgeClass += ' step-badge-scanStart'; label = 'scan start'; break;
    case 'eof':              badgeClass += ' step-badge-eof'; label = 'eof'; break;
  }

  stepInfoEl.innerHTML =
    '<span class="' + badgeClass + '">' + escapeHtml(label) + '</span> ' +
    '<span class="step-desc">' + escapeHtml(step.desc) + '</span>' +
    '<span class="step-pos">pos: ' + step.pos +
    (step.endPos !== step.pos ? ' \u2192 ' + step.endPos : '') + '</span>';
}

function renderLexerSourceForStep(step: LexerStep): void {
  let tokenBuildStart = -1;
  const data = stepData as LexerTraceResult;

  for (let i = stepIndex; i >= 0; i--) {
    if (data.steps[i].type === 'scanStart') {
      tokenBuildStart = data.steps[i].pos;
      break;
    }
    if (data.steps[i].type === 'emit' || data.steps[i].type === 'eof') {
      break;
    }
  }

  let html = '';
  const activeStart = step.pos;
  let activeEnd = step.endPos;

  if (step.type === 'emit') {
    tokenBuildStart = -1;
  }

  if (step.type === 'eof') {
    html += '<span class="consumed">' + escapeHtml(stepSource) + '</span>';
    editorDisplay.innerHTML = html;
    return;
  }

  let processedEnd = activeStart;
  if (tokenBuildStart >= 0 && tokenBuildStart < activeStart) {
    processedEnd = tokenBuildStart;
  }

  if (processedEnd > 0) {
    html += '<span class="consumed">' + escapeHtml(stepSource.substring(0, processedEnd)) + '</span>';
  }

  if (tokenBuildStart >= 0 && tokenBuildStart < activeStart && step.type !== 'emit') {
    html += '<span class="token-building">' + escapeHtml(stepSource.substring(tokenBuildStart, activeStart)) + '</span>';
  }

  if (activeEnd > activeStart) {
    html += '<mark class="micro-active">' + escapeHtml(stepSource.substring(activeStart, activeEnd)) + '</mark>';
  } else if (step.type === 'scanStart' || step.type === 'matchFail') {
    if (activeStart < stepSource.length) {
      html += '<mark class="micro-active">' + escapeHtml(stepSource.substring(activeStart, activeStart + 1)) + '</mark>';
      activeEnd = activeStart + 1;
    }
  }

  let remainStart = Math.max(activeEnd, processedEnd);
  if (tokenBuildStart >= 0 && tokenBuildStart < activeStart && step.type !== 'emit') {
    remainStart = activeEnd;
  }
  if (remainStart < stepSource.length) {
    html += escapeHtml(stepSource.substring(remainStart));
  }

  editorDisplay.innerHTML = html;
}

// --- Parser step rendering ---

function renderParserStepState(): void {
  const data = stepData as ParserTraceResult;
  const steps = data.steps;
  const step = steps[stepIndex];

  stepCounterEl.textContent = (stepIndex + 1) + ' / ' + steps.length;
  rebuildCallStack();
  renderParserStepInfo(step);
  renderParserSourceForStep(step);
  renderTokens(data.tokens, step.tokenPos);
  renderIncrementalAST();
}

function renderIncrementalAST(): void {
  const data = stepData as ParserTraceResult;
  const nodes: { depth: number; text: string; stepIdx: number }[] = [];
  for (let i = 0; i <= stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'produceNode') {
      nodes.push({ depth: s.depth, text: s.desc, stepIdx: i });
    }
  }

  if (nodes.length === 0) {
    panels.ast.innerHTML = '<span class="ast-empty">Program</span>';
    return;
  }

  const tree: typeof nodes = [];
  for (let i = 0; i < nodes.length; i++) {
    const node = nodes[i];
    let insertAt = tree.length;
    for (let j = tree.length - 1; j >= 0; j--) {
      if (tree[j].depth > node.depth) {
        insertAt = j;
      } else {
        break;
      }
    }
    tree.splice(insertAt, 0, node);
  }

  let newestIdx = -1;
  for (let i = 0; i <= stepIndex; i++) {
    if (data.steps[i].type === 'produceNode') {
      newestIdx = i;
    }
  }

  let html = '<div class="ast-tree">';
  html += '<div class="ast-node ast-program">Program</div>';
  for (let i = 0; i < tree.length; i++) {
    const n = tree[i];
    const indent = '  ' + '  '.repeat(n.depth + 1);
    let cls = 'ast-node';
    if (n.stepIdx === newestIdx) {
      cls += ' ast-node-new';
    }
    html += '<div class="' + cls + '">' +
      '<span class="ast-indent">' + indent + '</span>' +
      escapeHtml(n.text) + '</div>';
  }
  html += '</div>';
  panels.ast.innerHTML = html;

  const newNode = panels.ast.querySelector('.ast-node-new');
  if (newNode) {
    newNode.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  }
}

function rebuildCallStack(): void {
  const data = stepData as ParserTraceResult;
  parserCallStack = [];
  for (let i = 0; i <= stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'enterRule') {
      parserCallStack.push(s.rule);
    } else if (s.type === 'exitRule') {
      for (let j = parserCallStack.length - 1; j >= 0; j--) {
        if (parserCallStack[j] === s.rule) {
          parserCallStack.splice(j, 1);
          break;
        }
      }
    }
  }
}

function renderParserStepInfo(step: ParserStep): void {
  let badgeClass = 'step-badge';
  let label = step.type;
  switch (step.type) {
    case 'enterRule':    badgeClass += ' step-badge-enterRule'; label = 'enter'; break;
    case 'exitRule':     badgeClass += ' step-badge-exitRule'; label = 'exit'; break;
    case 'consumeToken': badgeClass += ' step-badge-consumeToken'; label = 'consume'; break;
    case 'tryMatch':     badgeClass += ' step-badge-tryMatch'; label = 'try match'; break;
    case 'produceNode':  badgeClass += ' step-badge-produceNode'; label = 'produce'; break;
    case 'error':        badgeClass += ' step-badge-error'; label = 'error'; break;
    case 'synchronize':  badgeClass += ' step-badge-synchronize'; label = 'sync'; break;
  }

  let stackHtml = '';
  if (parserCallStack.length > 0) {
    stackHtml = '<div class="parse-call-stack">';
    for (let i = 0; i < parserCallStack.length; i++) {
      if (i > 0) stackHtml += '<span class="stack-sep"> \u203A </span>';
      const isLast = i === parserCallStack.length - 1;
      stackHtml += '<span class="stack-item' + (isLast ? ' stack-current' : '') + '">' +
        escapeHtml(parserCallStack[i]) + '</span>';
    }
    stackHtml += '</div>';
  }

  const ruleHtml = step.rule ? '<span class="step-rule">' + escapeHtml(step.rule) + '</span> ' : '';

  stepInfoEl.innerHTML =
    '<span class="' + badgeClass + '">' + escapeHtml(label) + '</span> ' +
    ruleHtml +
    '<span class="step-desc">' + escapeHtml(step.desc) + '</span>' +
    '<span class="step-pos">tok: ' + step.tokenPos + '</span>' +
    stackHtml;
}

function renderParserSourceForStep(step: ParserStep): void {
  const data = stepData as ParserTraceResult;
  const tokenPos = step.tokenPos;
  const tokens = data.tokens;

  if (!tokens || tokens.length === 0) {
    editorDisplay.textContent = stepSource;
    return;
  }

  const currentToken = tokenPos < tokens.length ? tokens[tokenPos] : null;

  let consumedEnd = 0;
  if (tokenPos > 0 && tokenPos <= tokens.length) {
    const lastConsumed = tokens[tokenPos - 1];
    consumedEnd = lastConsumed.end || 0;
  }

  let html = '';

  if (!currentToken || tokenPos >= tokens.length) {
    html += '<span class="consumed">' + escapeHtml(stepSource) + '</span>';
    editorDisplay.innerHTML = html;
    return;
  }

  const highlightStart = currentToken.start || 0;
  const highlightEnd = currentToken.end || 0;

  if (consumedEnd > 0) {
    html += '<span class="consumed">' + escapeHtml(stepSource.substring(0, consumedEnd)) + '</span>';
  }

  if (consumedEnd < highlightStart) {
    html += escapeHtml(stepSource.substring(consumedEnd, highlightStart));
  }

  if (highlightEnd > highlightStart) {
    html += '<mark class="micro-active">' + escapeHtml(stepSource.substring(highlightStart, highlightEnd)) + '</mark>';
  }

  if (highlightEnd < stepSource.length) {
    html += escapeHtml(stepSource.substring(highlightEnd));
  }

  editorDisplay.innerHTML = html;
}

// --- Compiler step rendering ---

function renderCompilerStepState(): void {
  const data = stepData as CompilerTraceResult;
  const steps = data.steps;
  const step = steps[stepIndex];

  stepCounterEl.textContent = (stepIndex + 1) + ' / ' + steps.length;
  rebuildCompilerNodeStack();
  renderCompilerStepInfo(step);
  renderCompilerASTView();
  renderIncrementalBytecode();

  // Show source in the editor display (non-highlighted, just for context)
  editorDisplay.textContent = stepSource;
}

function rebuildCompilerNodeStack(): void {
  const data = stepData as CompilerTraceResult;
  compilerNodeStack = [];
  for (let i = 0; i <= stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'enterNode') {
      compilerNodeStack.push(s.nodeType + (s.desc ? '(' + s.desc + ')' : ''));
    } else if (s.type === 'exitNode') {
      compilerNodeStack.pop();
    }
  }
}

function renderCompilerStepInfo(step: CompilerStep): void {
  let badgeClass = 'step-badge';
  let label = step.type;
  switch (step.type) {
    case 'enterNode':        badgeClass += ' step-badge-enterNode'; label = 'enter'; break;
    case 'exitNode':         badgeClass += ' step-badge-exitNode'; label = 'exit'; break;
    case 'allocRegister':    badgeClass += ' step-badge-allocRegister'; label = 'alloc reg'; break;
    case 'emitInstruction':  badgeClass += ' step-badge-emitInstruction'; label = 'emit'; break;
    case 'addConstant':      badgeClass += ' step-badge-addConstant'; label = 'constant'; break;
    case 'patchJump':        badgeClass += ' step-badge-patchJump'; label = 'patch'; break;
    case 'pushLoop':         badgeClass += ' step-badge-pushLoop'; label = 'loop start'; break;
    case 'popLoop':          badgeClass += ' step-badge-popLoop'; label = 'loop end'; break;
  }

  let stackHtml = '';
  if (compilerNodeStack.length > 0) {
    stackHtml = '<div class="parse-call-stack">';
    for (let i = 0; i < compilerNodeStack.length; i++) {
      if (i > 0) stackHtml += '<span class="stack-sep"> \u203A </span>';
      const isLast = i === compilerNodeStack.length - 1;
      stackHtml += '<span class="stack-item' + (isLast ? ' stack-current' : '') + '">' +
        escapeHtml(compilerNodeStack[i]) + '</span>';
    }
    stackHtml += '</div>';
  }

  const nodeTypeHtml = step.nodeType
    ? '<span class="step-rule">' + escapeHtml(step.nodeType) + '</span> '
    : '';

  stepInfoEl.innerHTML =
    '<span class="' + badgeClass + '">' + escapeHtml(label) + '</span> ' +
    nodeTypeHtml +
    '<span class="step-desc">' + escapeHtml(step.desc) + '</span>' +
    stackHtml;
}

function renderCompilerASTView(): void {
  const data = stepData as CompilerTraceResult;
  if (!data.ast) {
    panels.ast.innerHTML = '<span class="ast-empty">No AST</span>';
    return;
  }

  // Determine the currently-active node by replaying the enter/exit trace
  // and tracking which occurrence of which nodeType we're currently inside.
  // "Occurrence" means: how many times we've entered this exact nodeType
  // across the entire trace up to this point.
  let activeNodeType = '';
  let activeOccurrence = -1;

  // Count total occurrences of each nodeType across all enterNode steps
  // up to stepIndex, and track which one is currently on the stack.
  const enterStack: { nodeType: string; occurrence: number }[] = [];
  const occurrenceCounts: Record<string, number> = {};
  for (let i = 0; i <= stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'enterNode') {
      const key = s.nodeType;
      occurrenceCounts[key] = (occurrenceCounts[key] || 0) + 1;
      enterStack.push({ nodeType: key, occurrence: occurrenceCounts[key] });
    } else if (s.type === 'exitNode') {
      enterStack.pop();
    }
  }
  if (enterStack.length > 0) {
    const top = enterStack[enterStack.length - 1];
    activeNodeType = top.nodeType;
    activeOccurrence = top.occurrence;
  }

  // Render AST lines. For each line that starts with the activeNodeType,
  // count occurrences and highlight only the matching one.
  const lines = data.ast.split('\n');
  const lineOccurrences: Record<string, number> = {};
  let html = '<div class="ast-tree">';
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line.trim() === '') continue;
    const trimmed = line.trimStart();
    const indent = line.substring(0, line.length - trimmed.length);

    // Extract the node type from the line (e.g. "VarDeclaration" from "VarDeclaration(Const a)")
    const match = trimmed.match(/^([A-Z][A-Za-z]*)/);
    let isActive = false;
    if (match) {
      const lineNodeType = match[1];
      lineOccurrences[lineNodeType] = (lineOccurrences[lineNodeType] || 0) + 1;
      if (lineNodeType === activeNodeType && lineOccurrences[lineNodeType] === activeOccurrence) {
        isActive = true;
      }
    }

    const cls = isActive ? 'ast-node ast-node-active' : 'ast-node';
    html += '<div class="' + cls + '">' +
      '<span class="ast-indent">' + escapeHtml(indent) + '</span>' +
      escapeHtml(trimmed) + '</div>';
  }
  html += '</div>';
  panels.ast.innerHTML = html;

  const activeNode = panels.ast.querySelector('.ast-node-active');
  if (activeNode) {
    activeNode.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  }
}

function formatOperands(instr: CompilerInstruction): string {
  const op = instr.opcode;
  // Match the disassembler format categories
  switch (op) {
    // ABC format
    case 'Add': case 'Sub': case 'Mul': case 'Div': case 'Mod': case 'Pow':
    case 'AddNum': case 'SubNum': case 'MulNum': case 'DivNum': case 'ModNum': case 'PowNum':
    case 'BitAnd': case 'BitOr': case 'BitXor':
    case 'ShiftLeft': case 'ShiftRight': case 'ShiftRightU':
    case 'Equal': case 'NotEqual': case 'LessThan': case 'LessEqual':
    case 'GreaterThan': case 'GreaterEqual': case 'StrictEqual': case 'StrictNotEqual':
    case 'GetProp': case 'SetProp': case 'GetIndex': case 'SetIndex':
    case 'Call':
      return 'R' + instr.a + ', R' + instr.b + ', R' + instr.c;

    // AB format
    case 'Move': case 'Neg': case 'NegNum': case 'BitNot': case 'Not': case 'TypeOf':
    case 'GetUpvalue': case 'SetUpvalue': case 'NewArray': case 'Print':
      return 'R' + instr.a + ', R' + instr.b;

    // ABx format
    case 'LoadConst': case 'GetGlobal': case 'SetGlobal': case 'Closure':
      return 'R' + instr.a + ', K' + instr.bx;

    // AsBx format (conditional jumps)
    case 'JumpIfTrue': case 'JumpIfFalse':
      return 'R' + instr.a + ', ' + instr.sbx;

    // Jump (sBx only)
    case 'Jump':
      return '' + instr.sbx;

    // A only
    case 'LoadNull': case 'LoadUndef': case 'LoadTrue': case 'LoadFalse':
    case 'NewObject': case 'CloseUpvalue': case 'Return':
      return 'R' + instr.a;

    // No operands
    case 'ReturnUndef':
      return '';

    default:
      return 'R' + instr.a + ', R' + instr.b + ', R' + instr.c;
  }
}

function simulateRegisters(
  instructions: CompilerInstruction[],
  instrCount: number,
  constants: string[],
): string[] {
  const regs: string[] = [];
  for (let i = 0; i < instrCount && i < instructions.length; i++) {
    const instr = instructions[i];
    const op = instr.opcode;
    const a = instr.a;

    // Ensure array is large enough
    while (regs.length <= a) regs.push('');

    switch (op) {
      case 'LoadConst':
        regs[a] = instr.bx < constants.length ? constants[instr.bx] : 'K' + instr.bx;
        break;
      case 'LoadNull':    regs[a] = 'null'; break;
      case 'LoadUndef':   regs[a] = 'undefined'; break;
      case 'LoadTrue':    regs[a] = 'true'; break;
      case 'LoadFalse':   regs[a] = 'false'; break;
      case 'Move':
        regs[a] = instr.b < regs.length && regs[instr.b] ? regs[instr.b] : 'R' + instr.b;
        break;
      case 'GetGlobal':
        regs[a] = instr.bx < constants.length ? 'global(' + constants[instr.bx] + ')' : 'global(K' + instr.bx + ')';
        break;
      case 'Neg': case 'NegNum':
        regs[a] = '-R' + instr.b;
        break;
      case 'Not':
        regs[a] = '!R' + instr.b;
        break;
      case 'BitNot':
        regs[a] = '~R' + instr.b;
        break;
      case 'TypeOf':
        regs[a] = 'typeof R' + instr.b;
        break;
      case 'Add': case 'AddNum': regs[a] = 'R' + instr.b + ' + R' + instr.c; break;
      case 'Sub': case 'SubNum': regs[a] = 'R' + instr.b + ' - R' + instr.c; break;
      case 'Mul': case 'MulNum': regs[a] = 'R' + instr.b + ' * R' + instr.c; break;
      case 'Div': case 'DivNum': regs[a] = 'R' + instr.b + ' / R' + instr.c; break;
      case 'Mod': case 'ModNum': regs[a] = 'R' + instr.b + ' % R' + instr.c; break;
      case 'Pow': case 'PowNum': regs[a] = 'R' + instr.b + ' ** R' + instr.c; break;
      case 'BitAnd':    regs[a] = 'R' + instr.b + ' & R' + instr.c; break;
      case 'BitOr':     regs[a] = 'R' + instr.b + ' | R' + instr.c; break;
      case 'BitXor':    regs[a] = 'R' + instr.b + ' ^ R' + instr.c; break;
      case 'ShiftLeft': regs[a] = 'R' + instr.b + ' << R' + instr.c; break;
      case 'ShiftRight':  regs[a] = 'R' + instr.b + ' >> R' + instr.c; break;
      case 'ShiftRightU': regs[a] = 'R' + instr.b + ' >>> R' + instr.c; break;
      case 'Equal':          regs[a] = 'R' + instr.b + ' == R' + instr.c; break;
      case 'NotEqual':       regs[a] = 'R' + instr.b + ' != R' + instr.c; break;
      case 'StrictEqual':    regs[a] = 'R' + instr.b + ' === R' + instr.c; break;
      case 'StrictNotEqual': regs[a] = 'R' + instr.b + ' !== R' + instr.c; break;
      case 'LessThan':      regs[a] = 'R' + instr.b + ' < R' + instr.c; break;
      case 'LessEqual':     regs[a] = 'R' + instr.b + ' <= R' + instr.c; break;
      case 'GreaterThan':   regs[a] = 'R' + instr.b + ' > R' + instr.c; break;
      case 'GreaterEqual':  regs[a] = 'R' + instr.b + ' >= R' + instr.c; break;
      case 'NewObject': regs[a] = '{}'; break;
      case 'NewArray':  regs[a] = '[]'; break;
      case 'Call':       regs[a] = 'call(R' + instr.b + ', ' + instr.c + ' args)'; break;
      case 'GetProp':    regs[a] = 'R' + instr.b + '[R' + instr.c + ']'; break;
      case 'GetIndex':   regs[a] = 'R' + instr.b + '[R' + instr.c + ']'; break;
      case 'Closure':    regs[a] = 'closure(K' + instr.bx + ')'; break;
      case 'GetUpvalue': regs[a] = 'upval(' + instr.b + ')'; break;
      // These don't write to a destination register
      case 'SetGlobal': case 'SetUpvalue': case 'SetProp': case 'SetIndex':
      case 'Jump': case 'JumpIfTrue': case 'JumpIfFalse':
      case 'Return': case 'ReturnUndef': case 'CloseUpvalue':
      case 'Print':
        break;
      default:
        regs[a] = op + '(...)';
        break;
    }
  }
  return regs;
}

function renderIncrementalBytecode(): void {
  const data = stepData as CompilerTraceResult;
  const step = data.steps[stepIndex];

  // Count how many instructions have been emitted up to current step
  let instrCount = 0;
  let newestInstrIndex = -1;
  let newestPatchIndex = -1;
  let highestRegister = -1;
  let newestRegister = -1;
  const constantsSoFar: number[] = [];

  for (let i = 0; i <= stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'emitInstruction' && s.instrIndex !== undefined) {
      instrCount = s.instrIndex + 1;
      newestInstrIndex = s.instrIndex;
    }
    if (s.type === 'patchJump' && s.instrIndex !== undefined) {
      newestPatchIndex = s.instrIndex;
    }
    if (s.type === 'allocRegister' && s.registerId !== undefined) {
      highestRegister = Math.max(highestRegister, s.registerId);
      newestRegister = s.registerId;
    }
    if (s.type === 'addConstant' && s.constantIndex !== undefined) {
      if (!constantsSoFar.includes(s.constantIndex)) {
        constantsSoFar.push(s.constantIndex);
      }
    }
  }

  // Simulate register contents from emitted instructions
  const regValues = simulateRegisters(data.instructions, instrCount, data.constants);

  // Build bytecode listing
  let html = '<div class="bytecode-listing">';

  for (let i = 0; i < instrCount && i < data.instructions.length; i++) {
    const instr = data.instructions[i];
    const addr = String(i).padStart(4, '0');
    const opName = instr.opcode;
    const operands = formatOperands(instr);

    let cls = 'bytecode-line';
    if (i === newestInstrIndex && step.type === 'emitInstruction') {
      cls += ' bytecode-line-new';
    }
    if (i === newestPatchIndex && step.type === 'patchJump') {
      cls += ' bytecode-line-patched';
    }

    // Add constant comment for ABx format instructions
    let comment = '';
    if ((opName === 'LoadConst' || opName === 'GetGlobal' || opName === 'SetGlobal' || opName === 'Closure') &&
        instr.bx < data.constants.length) {
      comment = ' ; ' + data.constants[instr.bx];
    }

    // Add jump target annotation
    if (opName === 'Jump' || opName === 'JumpIfTrue' || opName === 'JumpIfFalse') {
      const target = i + 1 + instr.sbx;
      comment = ' -> [' + String(target).padStart(4, '0') + ']';
    }

    html += '<div class="' + cls + '">' +
      '<span class="bytecode-addr">' + addr + '</span>  ' +
      '<span class="bytecode-op">' + escapeHtml(opName) + '</span>' +
      '<span class="bytecode-operands">' + escapeHtml(operands) + '</span>' +
      (comment ? '<span class="bytecode-comment">' + escapeHtml(comment) + '</span>' : '') +
      '</div>';
  }

  html += '</div>';

  // Register map
  if (highestRegister >= 0) {
    html += '<div class="bytecode-section">';
    html += '<div class="bytecode-section-header">Registers</div>';
    for (let r = 0; r <= highestRegister; r++) {
      const val = r < regValues.length && regValues[r] ? regValues[r] : '—';
      const isNew = step.type === 'allocRegister' && r === newestRegister;
      const cls = isNew ? 'register-entry register-new' : 'register-entry';
      html += '<div class="' + cls + '">R' + r + ' = ' + escapeHtml(val) + '</div>';
    }
    html += '</div>';
  }

  // Constants pool
  if (constantsSoFar.length > 0) {
    html += '<div class="bytecode-section">';
    html += '<div class="bytecode-section-header">Constants</div>';
    for (let i = 0; i < constantsSoFar.length; i++) {
      const idx = constantsSoFar[i];
      const isNew = step.type === 'addConstant' && step.constantIndex === idx;
      const cls = isNew ? 'constant-entry constant-new' : 'constant-entry';
      const val = idx < data.constants.length ? data.constants[idx] : '?';
      html += '<div class="' + cls + '">K' + idx + ' = ' + escapeHtml(val) + '</div>';
    }
    html += '</div>';
  }

  panels.bytecode.innerHTML = html;

  // Scroll to newest instruction
  const newLine = panels.bytecode.querySelector('.bytecode-line-new, .bytecode-line-patched');
  if (newLine) {
    newLine.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  }
}

// --- Build DOM ---

function buildPlayground(): void {
  const root = document.getElementById('app')!;

  // Nav
  const nav = el('nav', { className: 'app-nav' },
    el('a', { href: '../' }, 'Home'),
    el('a', { href: '../test262/' }, 'Test262 Runner'),
    el('a', { href: '../custom/' }, 'Custom Tests'),
  );

  const header = el('header', { className: 'app-header' },
    el('h1', {}, 'Yatsi'),
    el('p', { className: 'subtitle' }, 'Playground'),
    nav,
  );

  // Editor pane
  const editorLabel = el('label', {}, 'Source');

  editor = document.createElement('textarea');
  editor.id = 'editor';
  editor.spellcheck = false;
  editor.setAttribute('autocorrect', 'off');
  editor.setAttribute('autocapitalize', 'off');
  editor.placeholder = 'Type TypeScript here...';

  editorDisplay = el('pre', { className: 'editor-display hidden' });

  const editorPane = el('div', { className: 'editor-pane' },
    editorLabel,
    editor,
    editorDisplay,
  );

  // Tab bar
  const tabTokens = el('button', { 'data-tab': 'tokens', className: 'active' }, 'Tokens');
  const tabAst = el('button', { 'data-tab': 'ast' }, 'AST');
  const tabBytecode = el('button', { 'data-tab': 'bytecode' }, 'Bytecode');
  const tabOutput = el('button', { 'data-tab': 'output' }, 'Output');
  tabs = [tabTokens, tabAst, tabBytecode, tabOutput];

  // Step mode controls in tab bar area
  stepModeSelect = document.createElement('select');
  stepModeSelect.className = 'step-mode-select';
  const optLexer = el('option', { value: 'lexer' }, 'Lexer');
  const optParser = el('option', { value: 'parser' }, 'Parser');
  const optCompiler = el('option', { value: 'compiler' }, 'Compiler');
  stepModeSelect.appendChild(optLexer);
  stepModeSelect.appendChild(optParser);
  stepModeSelect.appendChild(optCompiler);

  stepToggleBtn = el('button', { className: 'step-toggle' }, 'Step');

  const tabBar = el('div', { className: 'tab-bar' },
    tabTokens, tabAst, tabBytecode, tabOutput,
    stepModeSelect, stepToggleBtn,
  );

  // Step controls bar
  stepResetBtn = el('button', {}, '\u23EE');
  stepPrevBtn = el('button', {}, '\u25C0');
  stepNextBtn = el('button', {}, '\u25B6');
  stepAutoBtn = el('button', { className: 'step-auto-btn' }, '\u25B6');
  stepExitBtn = el('button', {}, 'Exit');
  stepCounterEl = el('span', { className: 'step-counter' });

  stepControlsEl = el('div', { className: 'step-controls hidden' },
    stepResetBtn, stepPrevBtn, stepNextBtn, stepAutoBtn, stepExitBtn, stepCounterEl,
  );

  // Step info bar
  stepInfoEl = el('div', { className: 'step-info hidden' });

  // Panels
  const panelTokens = el('div', { id: 'panel-tokens', className: 'panel active' });
  const panelAst = el('div', { id: 'panel-ast', className: 'panel' });
  const panelBytecode = el('div', { id: 'panel-bytecode', className: 'panel' });
  const panelOutput = el('div', { id: 'panel-output', className: 'panel' });
  panels = { tokens: panelTokens, ast: panelAst, bytecode: panelBytecode, output: panelOutput };

  // Split labels for parser/compiler modes
  splitLabelTokens = el('div', { className: 'split-label', 'data-for': 'tokens' }, 'Tokens');
  splitLabelAst = el('div', { className: 'split-label', 'data-for': 'ast' }, 'AST');
  splitLabelBytecode = el('div', { className: 'split-label', 'data-for': 'bytecode' }, 'Bytecode');

  outputPane = el('div', { className: 'output-pane' },
    tabBar,
    stepControlsEl,
    stepInfoEl,
    splitLabelTokens, panelTokens,
    splitLabelAst, panelAst,
    splitLabelBytecode, panelBytecode,
    panelOutput,
  );

  // Main layout
  const playground = el('div', { className: 'playground' }, editorPane, outputPane);

  root.appendChild(header);
  root.appendChild(playground);
  root.appendChild(siteFooter());

  // --- Wire up events ---

  // Tab switching
  for (const btn of tabs) {
    btn.addEventListener('click', () => switchTab(btn.dataset.tab!));
  }

  // Editor input
  editor.addEventListener('input', debouncedRun);

  // Tab key in editor
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

  // Step mode select
  stepModeSelect.addEventListener('change', () => {
    stepModeType = stepModeSelect.value as 'lexer' | 'parser' | 'compiler';
    if (stepMode) {
      exitStepMode();
      enterStepMode();
    }
  });

  // Step mode buttons
  stepToggleBtn.addEventListener('click', () => {
    if (stepMode) exitStepMode();
    else enterStepMode();
  });
  stepNextBtn.addEventListener('click', stepNext);
  stepPrevBtn.addEventListener('click', stepPrev);
  stepResetBtn.addEventListener('click', stepReset);
  stepExitBtn.addEventListener('click', exitStepMode);
  stepAutoBtn.addEventListener('click', toggleAutoPlay);

  // Keyboard shortcuts
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
  buildPlayground();
  engine = await YatsiEngine.create();
  if (editor.value.trim()) {
    runPipeline();
  }
}

init();
