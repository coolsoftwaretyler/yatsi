import { el, siteFooter } from './ui/components';
import { YatsiEngine } from './engine/wasm-engine';

// --- Types (reused from playground) ---

interface CompilerStep {
  type: string;
  depth: number;
  nodeType: string;
  desc: string;
  instrIndex?: number;
  registerId?: number;
  constantIndex?: number;
  patchTarget?: number;
  line?: number;
  col?: number;
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

// --- Reconstructed loop context ---

interface ReconstructedLoopContext {
  loopType: string; // "while loop" or "for loop"
  continueTarget: number; // -1 if not yet known
  breakJumps: number[]; // instruction indices
  continueJumps: number[]; // instruction indices (for for-loops)
  isForLoop: boolean;
  pushStepIndex: number; // which trace step pushed this
}

// --- Example source snippets ---

const EXAMPLES: { title: string; source: string; description: string }[] = [
  // --- Basic control flow ---
  {
    title: 'if / else',
    source: `let x = 10;
if (x > 5) {
  x = 1;
} else {
  x = 2;
}`,
    description:
      'Conditional branching. The compiler emits JumpIfFalse to skip the "then" block, ' +
      'and an unconditional Jump to skip the "else" block. Both are patched once their targets are known.',
  },
  {
    title: 'while loop',
    source: `let i = 0;
while (i < 3) {
  i = i + 1;
}`,
    description:
      'The condition is evaluated at loop_start. JumpIfFalse exits the loop. ' +
      'An unconditional Jump goes back to loop_start. continue_target points to loop_start (the condition).',
  },
  {
    title: 'for loop',
    source: `let sum = 0;
for (let i = 0; i < 3; i = i + 1) {
  sum = sum + i;
}`,
    description:
      'The initializer runs once. The condition is at loop_start. After the body, ' +
      'the update expression runs. continue_target points to the update (not the condition). ' +
      'continue_jumps are deferred and patched once the update position is known.',
  },
  // --- More complex examples ---
  {
    title: 'nested if / else',
    source: `let x = 10;
let result = 0;
if (x > 5) {
  if (x > 8) {
    result = 3;
  } else {
    result = 2;
  }
} else {
  result = 1;
}`,
    description:
      'Nested conditionals produce multiple layers of jump_else / jump_end pairs. ' +
      'The inner if/else is fully compiled (and its jumps patched) before the outer else branch. ' +
      'Watch the Saved Jump Offsets panel show two jump_else variables at different times.',
  },
  {
    title: 'short-circuit && / ||',
    source: `let a = 5;
let b = 0;
let c = 10;
let x = a > 3 && b > 0;
let y = a > 3 || c < 2;`,
    description:
      '&& and || use short-circuit evaluation via skip_jump. For &&, if the left side is falsy ' +
      'the right side is skipped (JumpIfFalse). For ||, if the left is truthy the right is skipped ' +
      '(JumpIfTrue). Each skip_jump is emitted as a placeholder and patched after the right operand.',
  },
  {
    title: 'compound condition: && / || in if',
    source: `let a = 5;
let b = 0;
let c = 10;
if (a > 0 && b > 0 || c > 5) {
  a = 1;
}`,
    description:
      'When && and || are combined in a condition, multiple skip_jumps are emitted and patched in ' +
      'sequence. The && emits a JumpIfFalse (skip right of &&), then || emits a JumpIfTrue ' +
      '(skip right of ||). The nesting of logical operators produces a cascade of patches.',
  },
  {
    title: 'while with break / continue',
    source: `let i = 0;
let total = 0;
while (i < 10) {
  i = i + 1;
  if (i === 5) {
    continue;
  }
  if (i === 8) {
    break;
  }
  total = total + i;
}`,
    description:
      'continue emits a backward Jump to the condition (continue_target = loop_start) — not a ' +
      'placeholder, since the target is already known. break emits a forward Jump whose target ' +
      'is unknown at emit time — it goes into break_jumps and is patched after the loop body.',
  },
  {
    title: 'for with break / continue',
    source: `let sum = 0;
for (let i = 0; i < 10; i = i + 1) {
  if (i === 3) {
    continue;
  }
  if (i === 7) {
    break;
  }
  sum = sum + i;
}`,
    description:
      'In a for loop, continue must jump to the update expression, not the condition. ' +
      'But the update hasn\'t been compiled yet when continue is encountered, so the jump is deferred ' +
      'into continue_jumps and patched later. break_jumps are patched after the entire loop.',
  },
  {
    title: 'nested loops',
    source: `let result = 0;
for (let i = 0; i < 3; i = i + 1) {
  for (let j = 0; j < 3; j = j + 1) {
    if (i === j) {
      continue;
    }
    result = result + 1;
  }
}`,
    description:
      'Nested loops push two LoopContexts onto loop_stack_. break/continue always target the ' +
      'innermost (top-of-stack) loop. The inner loop is fully compiled — its jumps patched and context ' +
      'popped — before the outer loop\'s update runs. Watch the loop stack grow to depth 2.',
  },
];

// --- State per example ---

interface ExampleState {
  title: string;
  source: string;
  description: string;
  traceData: CompilerTraceResult | null;
  stepIndex: number;
  autoPlayTimer: ReturnType<typeof setTimeout> | null;
  // DOM refs
  editorEl: HTMLTextAreaElement;
  sourceOverlayEl: HTMLElement;
  stepInfoEl: HTMLElement;
  patchAnnotationEl: HTMLElement;
  jumpVarsEl: HTMLElement;
  stepCounterEl: HTMLElement;
  bytecodeEl: HTMLElement;
  loopContextEl: HTMLElement;
  nodeStackEl: HTMLElement;
  prevBtn: HTMLButtonElement;
  nextBtn: HTMLButtonElement;
  autoBtn: HTMLButtonElement;
  resetBtn: HTMLButtonElement;
  compileBtn: HTMLButtonElement;
  containerEl: HTMLElement;
}

let engine: YatsiEngine | null = null;
const states: ExampleState[] = [];
let initializing = true;

// --- Helpers ---

function escapeHtml(str: string): string {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

function formatOperands(instr: CompilerInstruction): string {
  const op = instr.opcode;
  switch (op) {
    case 'Add': case 'Sub': case 'Mul': case 'Div': case 'Mod': case 'Pow':
    case 'AddNum': case 'SubNum': case 'MulNum': case 'DivNum': case 'ModNum': case 'PowNum':
    case 'BitAnd': case 'BitOr': case 'BitXor':
    case 'ShiftLeft': case 'ShiftRight': case 'ShiftRightU':
    case 'Equal': case 'NotEqual': case 'LessThan': case 'LessEqual':
    case 'GreaterThan': case 'GreaterEqual': case 'StrictEqual': case 'StrictNotEqual':
    case 'GetProp': case 'SetProp': case 'GetIndex': case 'SetIndex':
    case 'Call':
      return 'R' + instr.a + ', R' + instr.b + ', R' + instr.c;
    case 'Move': case 'Neg': case 'NegNum': case 'BitNot': case 'Not': case 'TypeOf':
    case 'NewArray': case 'Print':
      return 'R' + instr.a + ', R' + instr.b;
    case 'GetUpvalue':
      return 'R' + instr.a + ', UV' + instr.b;
    case 'SetUpvalue':
      return 'UV' + instr.b + ', R' + instr.a;
    case 'LoadConst': case 'GetGlobal': case 'SetGlobal':
      return 'R' + instr.a + ', K' + instr.bx;
    case 'Closure':
      return 'R' + instr.a + ', F' + instr.bx;
    case 'JumpIfTrue': case 'JumpIfFalse':
      return 'R' + instr.a;
    case 'Jump':
      return '';
    case 'LoadNull': case 'LoadUndef': case 'LoadTrue': case 'LoadFalse':
    case 'NewObject': case 'CloseUpvalue': case 'Return':
      return 'R' + instr.a;
    case 'ReturnUndef':
      return '';
    default:
      return 'R' + instr.a + ', R' + instr.b + ', R' + instr.c;
  }
}

// --- Reconstruct loop context from trace steps ---

function reconstructLoopStack(
  steps: CompilerStep[],
  upToIndex: number,
): ReconstructedLoopContext[] {
  const stack: ReconstructedLoopContext[] = [];

  for (let i = 0; i <= upToIndex; i++) {
    const s = steps[i];

    if (s.type === 'pushLoop') {
      const isFor = s.desc.includes('for');
      stack.push({
        loopType: s.desc,
        continueTarget: -1,
        breakJumps: [],
        continueJumps: [],
        isForLoop: isFor,
        pushStepIndex: i,
      });

      // For while loops, the continue_target is the instruction offset
      // at the time of push. We find the most recent emitInstruction to get it.
      if (!isFor) {
        let lastInstrIndex = -1;
        for (let j = i - 1; j >= 0; j--) {
          if (steps[j].type === 'emitInstruction' && steps[j].instrIndex !== undefined) {
            lastInstrIndex = steps[j].instrIndex! + 1;
            break;
          }
        }
        if (lastInstrIndex === -1) lastInstrIndex = 0;
        stack[stack.length - 1].continueTarget = lastInstrIndex;
      }
    } else if (s.type === 'popLoop') {
      stack.pop();
    } else if (s.type === 'patchJump' && stack.length > 0) {
      // Track which jumps are being patched - this helps us identify
      // break/continue patches
    } else if (s.type === 'emitInstruction' && stack.length > 0) {
      const ctx = stack[stack.length - 1];
      // When a Jump instruction is emitted inside a loop body, check
      // if the next step patches it or if it goes into break/continue
      // We track emitted jump instructions to correlate with break/continue
      if (s.instrIndex !== undefined) {
        // Look ahead to see if the NEXT step is a break/continue annotation
        // Actually, let's look at the node stack for BreakStmt/ContinueStmt
        // Check if we're inside a BreakStmt or ContinueStmt by replaying enter/exit
        const nodeStack: string[] = [];
        for (let j = 0; j <= i; j++) {
          if (steps[j].type === 'enterNode') nodeStack.push(steps[j].nodeType);
          else if (steps[j].type === 'exitNode') nodeStack.pop();
        }

        if (nodeStack.includes('BreakStmt') && s.desc.includes('Jump')) {
          ctx.breakJumps.push(s.instrIndex);
        } else if (nodeStack.includes('ContinueStmt') && s.desc.includes('Jump')) {
          if (ctx.isForLoop) {
            ctx.continueJumps.push(s.instrIndex);
          }
        }
      }
    }

    // For for-loops: detect when continue_target gets set
    // This happens right before the update expression, when continue_jumps are patched
    if (s.type === 'patchJump' && stack.length > 0) {
      const ctx = stack[stack.length - 1];
      if (ctx.isForLoop && s.patchTarget !== undefined && ctx.continueTarget === -1) {
        // Check if this patch corresponds to a continue jump
        if (s.instrIndex !== undefined && ctx.continueJumps.includes(s.instrIndex)) {
          ctx.continueTarget = s.patchTarget;
        }
      }
    }
  }

  return stack;
}

// --- Compile an example ---

function compileExample(state: ExampleState): void {
  if (!engine) return;

  const source = state.editorEl.value;
  if (source.trim() === '') return;

  try {
    const json = engine.compileTraced(source);
    state.traceData = JSON.parse(json);
  } catch (e) {
    state.bytecodeEl.innerHTML =
      '<div class="error">Trace error: ' + escapeHtml((e as Error).message) + '</div>';
    return;
  }

  if (!state.traceData || !state.traceData.steps || state.traceData.steps.length === 0) return;

  if (state.traceData.errors && state.traceData.errors.length > 0) {
    let errHtml = '';
    for (const err of state.traceData.errors) {
      errHtml += '<div class="error">' + escapeHtml(err) + '</div>';
    }
    state.bytecodeEl.innerHTML = errHtml;
    return;
  }

  state.stepIndex = 0;
  state.containerEl.classList.add('cf-active');
  state.compileBtn.textContent = 'Re-compile';
  renderState(state);
}

// --- Step controls ---

function stepNext(state: ExampleState): void {
  if (!state.traceData) return;
  if (state.stepIndex < state.traceData.steps.length - 1) {
    state.stepIndex++;
    renderState(state);
  } else {
    stopAutoPlay(state);
  }
}

function stepPrev(state: ExampleState): void {
  if (!state.traceData) return;
  if (state.stepIndex > 0) {
    state.stepIndex--;
    renderState(state);
  }
}

function stepReset(state: ExampleState): void {
  if (!state.traceData) return;
  stopAutoPlay(state);
  state.stepIndex = 0;
  renderState(state);
}

function toggleAutoPlay(state: ExampleState): void {
  if (!state.traceData) return;
  if (state.autoPlayTimer) {
    stopAutoPlay(state);
  } else {
    startAutoPlay(state);
  }
}

function startAutoPlay(state: ExampleState): void {
  if (!state.traceData) return;
  if (state.stepIndex >= state.traceData.steps.length - 1) {
    state.stepIndex = 0;
    renderState(state);
  }
  state.autoBtn.innerHTML = '&#9646;&#9646;';
  state.autoBtn.classList.add('active');
  autoPlayTick(state);
}

function autoPlayTick(state: ExampleState): void {
  if (!state.traceData) { stopAutoPlay(state); return; }
  if (state.stepIndex >= state.traceData.steps.length - 1) { stopAutoPlay(state); return; }

  state.stepIndex++;
  renderState(state);

  const step = state.traceData.steps[state.stepIndex];
  let delay = 300;
  switch (step.type) {
    case 'enterNode': case 'exitNode': delay = 150; break;
    case 'allocRegister': delay = 200; break;
    case 'emitInstruction': delay = 400; break;
    case 'addConstant': delay = 350; break;
    case 'patchJump': delay = 600; break;
    case 'pushLoop': case 'popLoop': delay = 500; break;
  }
  state.autoPlayTimer = setTimeout(() => autoPlayTick(state), delay);
}

function stopAutoPlay(state: ExampleState): void {
  if (state.autoPlayTimer) {
    clearTimeout(state.autoPlayTimer);
    state.autoPlayTimer = null;
  }
  state.autoBtn.innerHTML = '&#9654;';
  state.autoBtn.classList.remove('active');
}

// --- Rendering ---

function renderState(state: ExampleState): void {
  if (!state.traceData) return;
  const data = state.traceData;
  const step = data.steps[state.stepIndex];

  state.stepCounterEl.textContent =
    (state.stepIndex + 1) + ' / ' + data.steps.length;

  renderStepInfo(state, step);
  renderPatchAnnotation(state);
  renderNodeStack(state);
  renderJumpVars(state);
  renderBytecode(state);
  renderLoopContext(state);
  renderSourceHighlight(state);

  // Update button states
  state.resetBtn.disabled = state.stepIndex === 0;
  state.prevBtn.disabled = state.stepIndex === 0;
  state.nextBtn.disabled = state.stepIndex >= data.steps.length - 1;
}

function renderStepInfo(state: ExampleState, step: CompilerStep): void {
  let badgeClass = 'step-badge';
  let label = step.type;
  switch (step.type) {
    case 'enterNode':        badgeClass += ' step-badge-enterNode'; label = 'enter'; break;
    case 'exitNode':         badgeClass += ' step-badge-exitNode'; label = 'exit'; break;
    case 'allocRegister':    badgeClass += ' step-badge-allocRegister'; label = 'alloc reg'; break;
    case 'emitInstruction':  badgeClass += ' step-badge-emitInstruction'; label = 'emit'; break;
    case 'addConstant':      badgeClass += ' step-badge-addConstant'; label = 'constant'; break;
    case 'patchJump':        badgeClass += ' step-badge-patchJump'; label = 'patch'; break;
    case 'pushLoop':         badgeClass += ' step-badge-pushLoop'; label = 'loop push'; break;
    case 'popLoop':          badgeClass += ' step-badge-popLoop'; label = 'loop pop'; break;
  }

  const nodeTypeHtml = step.nodeType
    ? '<span class="step-rule">' + escapeHtml(step.nodeType) + '</span> '
    : '';

  state.stepInfoEl.innerHTML =
    '<span class="' + badgeClass + '">' + escapeHtml(label) + '</span> ' +
    nodeTypeHtml +
    '<span class="step-desc">' + escapeHtml(step.desc) + '</span>';
}

function renderPatchAnnotation(state: ExampleState): void {
  if (!state.traceData) {
    state.patchAnnotationEl.classList.add('hidden');
    return;
  }

  const data = state.traceData;
  const step = data.steps[state.stepIndex];

  if (step.type !== 'patchJump') {
    state.patchAnnotationEl.classList.add('hidden');
    return;
  }

  // Build the node stack at this point
  const nodeStack: string[] = [];
  for (let i = 0; i <= state.stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'enterNode') nodeStack.push(s.nodeType);
    else if (s.type === 'exitNode') nodeStack.pop();
  }

  // Format the address for display
  const fromAddr = step.instrIndex !== undefined
    ? '[' + String(step.instrIndex).padStart(4, '0') + ']'
    : '[????]';
  const toAddr = step.patchTarget !== undefined
    ? '[' + String(step.patchTarget).padStart(4, '0') + ']'
    : '[????]';

  // Determine the instruction opcode at the patch source
  let patchOpcode = '';
  if (step.instrIndex !== undefined && step.instrIndex < data.instructions.length) {
    patchOpcode = data.instructions[step.instrIndex].opcode;
  }

  // Check loop context to detect break/continue patches
  const loopStack = reconstructLoopStack(data.steps, state.stepIndex);
  let isBreakPatch = false;
  let isContinuePatch = false;
  if (loopStack.length > 0) {
    const topLoop = loopStack[loopStack.length - 1];
    if (step.instrIndex !== undefined) {
      isBreakPatch = topLoop.breakJumps.includes(step.instrIndex);
      isContinuePatch = topLoop.continueJumps.includes(step.instrIndex);
    }
  }

  // Check if we just exited a loop (popLoop just happened)
  let justPoppedLoop = false;
  if (state.stepIndex > 0) {
    const prevStep = data.steps[state.stepIndex - 1];
    if (prevStep.type === 'popLoop') justPoppedLoop = true;
  }

  let annotation = '';

  if (isBreakPatch || justPoppedLoop) {
    if (isBreakPatch) {
      annotation = 'When the break was compiled, emit_jump returned index ' +
        fromAddr + ' and it was saved into break_jumps. ' +
        'Now the loop is done, the compiler iterates break_jumps and calls ' +
        'patch_jump(' + fromAddr + '). current_offset()  is ' + toAddr +
        ', so the break lands here, past the loop.';
    } else {
      annotation = 'When the condition was compiled, emit_jump(JumpIfFalse) returned index ' +
        fromAddr + ', saved in exit_jump. ' +
        'Now the loop body and backward jump are compiled, the compiler calls ' +
        'patch_jump(exit_jump). current_offset()  is ' + toAddr +
        ', so a false condition exits here.';
    }
  } else if (isContinuePatch) {
    annotation = 'When continue was compiled, emit_jump returned index ' +
      fromAddr + ' and it was saved into continue_jumps. ' +
      'Now the body is done, current_offset() is ' + toAddr +
      ' (the update expression). The compiler iterates continue_jumps and calls ' +
      'patch_jump_to(' + fromAddr + ', ' + toAddr + ').';
  } else if (nodeStack.includes('IfStmt')) {
    if (patchOpcode === 'JumpIfFalse') {
      annotation = 'When the condition was compiled, emit_jump(JumpIfFalse) returned index ' +
        fromAddr + ', saved in jump_else. ' +
        'Now the then-block is done, the compiler calls ' +
        'patch_jump(jump_else). current_offset()  is ' + toAddr +
        ', so a false condition skips to here.';
    } else if (patchOpcode === 'Jump') {
      annotation = 'At the end of the then-block, emit_jump(Jump) returned index ' +
        fromAddr + ', saved in jump_end. ' +
        'Now the else-block is done, the compiler calls ' +
        'patch_jump(jump_end). current_offset()  is ' + toAddr +
        ', so the then-block jumps over the else to here.';
    } else {
      annotation = 'Patching jump at ' + fromAddr + ': the compiler saved this index ' +
        'when the placeholder was emitted. current_offset() is now ' + toAddr + '.';
    }
  } else if (nodeStack.includes('WhileStmt')) {
    if (patchOpcode === 'JumpIfFalse') {
      annotation = 'When the condition was compiled, emit_jump(JumpIfFalse) returned index ' +
        fromAddr + ', saved in exit_jump. ' +
        'Now the body and backward jump are compiled, the compiler calls ' +
        'patch_jump(exit_jump). current_offset()  is ' + toAddr +
        ', so a false condition exits the loop here.';
    } else {
      annotation = 'Patching jump at ' + fromAddr + ': the compiler saved this index ' +
        'when the placeholder was emitted. current_offset() is now ' + toAddr + '.';
    }
  } else if (nodeStack.includes('ForStmt')) {
    if (patchOpcode === 'JumpIfFalse') {
      annotation = 'When the condition was compiled, emit_jump(JumpIfFalse) returned index ' +
        fromAddr + ', saved in exit_jump. ' +
        'Now the body, update, and backward jump are compiled, the compiler calls ' +
        'patch_jump(exit_jump). current_offset()  is ' + toAddr +
        ', so a false condition exits the loop here.';
    } else if (patchOpcode === 'Jump') {
      annotation = 'emit_jump(Jump) returned index ' + fromAddr +
        ', which was saved. Now the compiler calls ' +
        'patch_jump. current_offset()  is ' + toAddr + '.';
    } else {
      annotation = 'Patching jump at ' + fromAddr + ': the compiler saved this index ' +
        'when the placeholder was emitted. current_offset() is now ' + toAddr + '.';
    }
  } else if (nodeStack.includes('LogicalExpr')) {
    if (patchOpcode === 'JumpIfFalse') {
      annotation = 'Short-circuit &&: emit_jump(JumpIfFalse) returned index ' +
        fromAddr + ', saved in skip_jump. ' +
        'Now the right operand is compiled, the compiler calls ' +
        'patch_jump(skip_jump). current_offset()  is ' + toAddr +
        '. If the left side was falsy, it skips the right side.';
    } else if (patchOpcode === 'JumpIfTrue') {
      annotation = 'Short-circuit ||: emit_jump(JumpIfTrue) returned index ' +
        fromAddr + ', saved in skip_jump. ' +
        'Now the right operand is compiled, the compiler calls ' +
        'patch_jump(skip_jump). current_offset()  is ' + toAddr +
        '. If the left side was truthy, it skips the right side.';
    } else {
      annotation = 'Patching short-circuit jump at ' + fromAddr + ': the compiler saved ' +
        'this index when the placeholder was emitted. current_offset() is now ' + toAddr + '.';
    }
  } else {
    annotation = 'Patching jump at ' + fromAddr + ': the compiler saved this index when ' +
      'the placeholder was emitted. current_offset() is now ' + toAddr + '.';
  }

  state.patchAnnotationEl.textContent = annotation;
  state.patchAnnotationEl.classList.remove('hidden');
}

function renderNodeStack(state: ExampleState): void {
  if (!state.traceData) return;
  const data = state.traceData;

  const nodeStack: string[] = [];
  for (let i = 0; i <= state.stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'enterNode') {
      nodeStack.push(s.nodeType + (s.desc ? '(' + s.desc + ')' : ''));
    } else if (s.type === 'exitNode') {
      nodeStack.pop();
    }
  }

  if (nodeStack.length === 0) {
    state.nodeStackEl.innerHTML = '<span class="cf-empty">--</span>';
    return;
  }

  let html = '';
  for (let i = 0; i < nodeStack.length; i++) {
    if (i > 0) html += '<span class="stack-sep"> \u203A </span>';
    const isLast = i === nodeStack.length - 1;
    html += '<span class="stack-item' + (isLast ? ' stack-current' : '') + '">' +
      escapeHtml(nodeStack[i]) + '</span>';
  }
  state.nodeStackEl.innerHTML = html;
}

function renderBytecode(state: ExampleState): void {
  if (!state.traceData) return;
  const data = state.traceData;
  const step = data.steps[state.stepIndex];

  let instrCount = 0;
  let newestInstrIndex = -1;
  let newestPatchIndex = -1;
  let patchTarget = -1;
  let newestRegisterId = -1;
  const constantsSoFar: number[] = [];
  let registerCount = 0;

  // Collect all patches up to current step
  const allPatches: { from: number; to: number; stepIdx: number }[] = [];

  for (let i = 0; i <= state.stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'emitInstruction' && s.instrIndex !== undefined) {
      instrCount = s.instrIndex + 1;
      newestInstrIndex = s.instrIndex;
    }
    if (s.type === 'patchJump' && s.instrIndex !== undefined) {
      newestPatchIndex = s.instrIndex;
      if (s.patchTarget !== undefined) {
        patchTarget = s.patchTarget;
        allPatches.push({ from: s.instrIndex, to: s.patchTarget, stepIdx: i });
      }
    }
    if (s.type === 'addConstant' && s.constantIndex !== undefined) {
      if (!constantsSoFar.includes(s.constantIndex)) {
        constantsSoFar.push(s.constantIndex);
      }
    }
    if (s.type === 'allocRegister' && s.registerId !== undefined) {
      registerCount = s.registerId + 1;
      newestRegisterId = s.registerId;
    }
  }

  // Simulate register contents by replaying emitted instructions
  const registerDescs: string[] = new Array(registerCount).fill('');
  for (let idx = 0; idx < instrCount && idx < data.instructions.length; idx++) {
    const instr = data.instructions[idx];
    const op = instr.opcode;
    const a = instr.a;
    if (a >= registerCount) continue;

    switch (op) {
      case 'LoadConst': {
        const val = instr.bx < data.constants.length ? data.constants[instr.bx] : 'K' + instr.bx;
        registerDescs[a] = val;
        break;
      }
      case 'LoadTrue': registerDescs[a] = 'true'; break;
      case 'LoadFalse': registerDescs[a] = 'false'; break;
      case 'LoadNull': registerDescs[a] = 'null'; break;
      case 'LoadUndef': registerDescs[a] = 'undefined'; break;
      case 'GetGlobal': {
        const name = instr.bx < data.constants.length ? data.constants[instr.bx] : 'K' + instr.bx;
        registerDescs[a] = name;
        break;
      }
      case 'SetGlobal': break;
      case 'Move': registerDescs[a] = registerDescs[instr.b] || 'R' + instr.b; break;
      case 'Add': case 'Sub': case 'Mul': case 'Div': case 'Mod': case 'Pow':
      case 'BitAnd': case 'BitOr': case 'BitXor':
      case 'ShiftLeft': case 'ShiftRight': case 'ShiftRightU': {
        const opSym: Record<string, string> = {
          Add: '+', Sub: '-', Mul: '*', Div: '/', Mod: '%', Pow: '**',
          BitAnd: '&', BitOr: '|', BitXor: '^',
          ShiftLeft: '<<', ShiftRight: '>>', ShiftRightU: '>>>',
        };
        const lb = registerDescs[instr.b] || 'R' + instr.b;
        const rc = registerDescs[instr.c] || 'R' + instr.c;
        registerDescs[a] = lb + ' ' + (opSym[op] || op) + ' ' + rc;
        break;
      }
      case 'Equal': case 'NotEqual': case 'StrictEqual': case 'StrictNotEqual':
      case 'LessThan': case 'LessEqual': case 'GreaterThan': case 'GreaterEqual': {
        const cmpSym: Record<string, string> = {
          Equal: '==', NotEqual: '!=', StrictEqual: '===', StrictNotEqual: '!==',
          LessThan: '<', LessEqual: '<=', GreaterThan: '>', GreaterEqual: '>=',
        };
        const lb = registerDescs[instr.b] || 'R' + instr.b;
        const rc = registerDescs[instr.c] || 'R' + instr.c;
        registerDescs[a] = lb + ' ' + (cmpSym[op] || op) + ' ' + rc;
        break;
      }
      case 'Neg': registerDescs[a] = '-(' + (registerDescs[instr.b] || 'R' + instr.b) + ')'; break;
      case 'Not': registerDescs[a] = '!(' + (registerDescs[instr.b] || 'R' + instr.b) + ')'; break;
      case 'BitNot': registerDescs[a] = '~(' + (registerDescs[instr.b] || 'R' + instr.b) + ')'; break;
      case 'TypeOf': registerDescs[a] = 'typeof ' + (registerDescs[instr.b] || 'R' + instr.b); break;
      case 'Print': break;
      default: {
        if (op !== 'Jump' && op !== 'JumpIfTrue' && op !== 'JumpIfFalse') {
          registerDescs[a] = '<' + op + '>';
        }
        break;
      }
    }
  }

  // Build lines with jump target annotations
  let html = '<div class="bytecode-listing">';

  // Track which jump instructions were emitted as placeholders (will be patched
  // at some point in the full trace). A jump not in this set was emitted with a
  // real offset (e.g. backward loop-back jumps).
  const allPatchTargets = new Set<number>();
  for (const s of data.steps) {
    if (s.type === 'patchJump' && s.instrIndex !== undefined) {
      allPatchTargets.add(s.instrIndex);
    }
  }

  // Track which placeholders have been patched so far at this step
  const patchedJumps = new Map<number, number>();
  for (const p of allPatches) {
    patchedJumps.set(p.from, p.to);
  }

  // Collect labels - which instructions are jump targets
  const jumpTargets = new Set<number>();
  for (const [, target] of patchedJumps) {
    jumpTargets.add(target);
  }
  // Also add targets from non-placeholder jumps (backward jumps emitted with real offsets)
  for (let i = 0; i < instrCount && i < data.instructions.length; i++) {
    const instr = data.instructions[i];
    if (instr.opcode === 'Jump' || instr.opcode === 'JumpIfTrue' || instr.opcode === 'JumpIfFalse') {
      if (!allPatchTargets.has(i)) {
        jumpTargets.add(i + 1 + instr.sbx);
      }
    }
  }

  for (let i = 0; i < instrCount && i < data.instructions.length; i++) {
    const instr = data.instructions[i];
    const addr = String(i).padStart(4, '0');
    const opName = instr.opcode;
    const isJump = opName === 'Jump' || opName === 'JumpIfTrue' || opName === 'JumpIfFalse';

    const isPlaceholder = isJump && allPatchTargets.has(i);
    const isPatched = patchedJumps.has(i);

    const operands = formatOperands(instr);

    let cls = 'bytecode-line';
    if (i === newestInstrIndex && step.type === 'emitInstruction') {
      cls += ' bytecode-line-new';
    }
    if (i === newestPatchIndex && step.type === 'patchJump') {
      cls += ' bytecode-line-patched';
    }

    // Mark jump target lines
    let targetLabel = '';
    if (jumpTargets.has(i)) {
      targetLabel = '<span class="cf-jump-label">\u25B8</span>';
    }

    // Constant comment
    let comment = '';
    if ((opName === 'LoadConst' || opName === 'GetGlobal' || opName === 'SetGlobal' || opName === 'Closure') &&
        instr.bx < data.constants.length) {
      comment = ' ; ' + data.constants[instr.bx];
    }

    // Jump target annotation
    if (isJump) {
      if (isPlaceholder && !isPatched) {
        comment = ' -> [????] (unpatched)';
      } else if (isPlaceholder && isPatched) {
        const target = patchedJumps.get(i)!;
        comment = ' -> [' + String(target).padStart(4, '0') + ']';
      } else {
        const target = i + 1 + instr.sbx;
        comment = ' -> [' + String(target).padStart(4, '0') + ']';
      }
    }

    // If this was just patched on this step, show special annotation
    if (i === newestPatchIndex && step.type === 'patchJump' && patchTarget >= 0) {
      comment = ' -> [' + String(patchTarget).padStart(4, '0') + '] (just patched!)';
    }

    html += '<div class="' + cls + '">' +
      targetLabel +
      '<span class="bytecode-addr">' + addr + '</span>  ' +
      '<span class="bytecode-op">' + escapeHtml(opName) + '</span> ' +
      '<span class="bytecode-operands">' + escapeHtml(operands) + '</span>' +
      (comment ? '<span class="bytecode-comment">' + escapeHtml(comment) + '</span>' : '') +
      '</div>';
  }

  if (instrCount === 0) {
    html += '<div class="cf-empty">No instructions emitted yet</div>';
  }

  html += '</div>';

  // Constants pool
  if (constantsSoFar.length > 0) {
    html += '<div class="bytecode-section">';
    html += '<div class="bytecode-section-header">Constants</div>';
    for (const idx of constantsSoFar) {
      const isNew = step.type === 'addConstant' && step.constantIndex === idx;
      const cls = isNew ? 'constant-entry constant-new' : 'constant-entry';
      const val = idx < data.constants.length ? data.constants[idx] : '?';
      html += '<div class="' + cls + '">K' + idx + ' = ' + escapeHtml(val) + '</div>';
    }
    html += '</div>';
  }

  // Registers
  if (registerCount > 0) {
    html += '<div class="bytecode-section">';
    html += '<div class="bytecode-section-header">Registers</div>';
    for (let r = 0; r < registerCount; r++) {
      const isNew = step.type === 'allocRegister' && r === newestRegisterId;
      const desc = registerDescs[r];
      const cls = isNew ? 'register-entry register-new' : 'register-entry';
      html += '<div class="' + cls + '">R' + r;
      if (desc) {
        html += ' = ' + escapeHtml(desc);
      }
      html += '</div>';
    }
    html += '</div>';
  }

  state.bytecodeEl.innerHTML = html;

  // Scroll to newest (skip during initial auto-compile)
  if (!initializing) {
    const newLine = state.bytecodeEl.querySelector('.bytecode-line-new, .bytecode-line-patched');
    if (newLine) {
      newLine.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

function renderLoopContext(state: ExampleState): void {
  if (!state.traceData) return;
  const data = state.traceData;
  const step = data.steps[state.stepIndex];

  const loopStack = reconstructLoopStack(data.steps, state.stepIndex);

  if (loopStack.length === 0) {
    // Check if we're on a pushLoop/popLoop step for messaging
    if (step.type === 'popLoop') {
      state.loopContextEl.innerHTML =
        '<div class="cf-loop-event cf-loop-pop">' +
        '<span class="cf-loop-event-badge cf-loop-pop-badge">popped</span>' +
        ' Loop context removed from stack' +
        '</div>' +
        '<div class="cf-empty">loop_stack is empty</div>';
    } else {
      state.loopContextEl.innerHTML = '<div class="cf-empty">loop_stack is empty</div>';
    }
    return;
  }

  let html = '';

  // Show event badge for pushLoop/popLoop transitions
  if (step.type === 'pushLoop') {
    html +=
      '<div class="cf-loop-event cf-loop-push">' +
      '<span class="cf-loop-event-badge cf-loop-push-badge">pushed</span>' +
      ' New loop context added to stack' +
      '</div>';
  }

  html += '<div class="cf-loop-stack-label">loop_stack_ (' + loopStack.length + ' deep)</div>';

  for (let i = loopStack.length - 1; i >= 0; i--) {
    const ctx = loopStack[i];
    const isTop = i === loopStack.length - 1;

    html += '<div class="cf-loop-ctx' + (isTop ? ' cf-loop-ctx-top' : '') + '">';
    html += '<div class="cf-loop-ctx-header">';
    html += '<span class="cf-loop-type">' + escapeHtml(ctx.loopType) + '</span>';
    if (isTop) html += '<span class="cf-loop-top-badge">TOP</span>';
    html += '</div>';

    html += '<div class="cf-loop-fields">';

    // continue_target
    html += '<div class="cf-field">';
    html += '<span class="cf-field-name">continue_target</span>';
    if (ctx.continueTarget >= 0) {
      html += '<span class="cf-field-value cf-field-value-set">[' +
        String(ctx.continueTarget).padStart(4, '0') + ']</span>';
    } else {
      html += '<span class="cf-field-value cf-field-value-unknown">unknown (deferred)</span>';
    }
    html += '</div>';

    // is_for_loop
    html += '<div class="cf-field">';
    html += '<span class="cf-field-name">is_for_loop</span>';
    html += '<span class="cf-field-value">' + ctx.isForLoop + '</span>';
    html += '</div>';

    // break_jumps
    html += '<div class="cf-field">';
    html += '<span class="cf-field-name">break_jumps</span>';
    if (ctx.breakJumps.length === 0) {
      html += '<span class="cf-field-value cf-field-value-empty">[]</span>';
    } else {
      html += '<span class="cf-field-value cf-field-value-set">[' +
        ctx.breakJumps.map(j => String(j).padStart(4, '0')).join(', ') + ']</span>';
    }
    html += '</div>';

    // continue_jumps (mainly for for-loops)
    html += '<div class="cf-field">';
    html += '<span class="cf-field-name">continue_jumps</span>';
    if (ctx.continueJumps.length === 0) {
      html += '<span class="cf-field-value cf-field-value-empty">[]</span>';
    } else {
      html += '<span class="cf-field-value cf-field-value-set">[' +
        ctx.continueJumps.map(j => String(j).padStart(4, '0')).join(', ') + ']</span>';
    }
    html += '</div>';

    html += '</div>'; // fields
    html += '</div>'; // ctx
  }

  state.loopContextEl.innerHTML = html;
}

// --- Reconstruct saved jump variables from trace ---
// The compiler uses local variables (jump_else, jump_end, exit_jump, skip_jump)
// scoped to each AST node visitor. We reconstruct them by watching emit/patch
// patterns in the trace, keyed to the node stack.

interface SavedJumpVar {
  name: string;        // C++ variable name: jump_else, jump_end, exit_jump, skip_jump
  instrIndex: number;  // the instruction index saved by emit_jump
  nodeType: string;    // which AST node owns this variable
  patched: boolean;    // has this been patched yet?
  patchTarget?: number;
}

function reconstructJumpVars(
  steps: CompilerStep[],
  upToIndex: number,
): SavedJumpVar[] {
  const vars: SavedJumpVar[] = [];
  const nodeStack: string[] = [];

  // Track which instructions will eventually be patched (from full trace)
  const allPatchTargets = new Set<number>();
  for (const s of steps) {
    if (s.type === 'patchJump' && s.instrIndex !== undefined) {
      allPatchTargets.add(s.instrIndex);
    }
  }

  for (let i = 0; i <= upToIndex; i++) {
    const s = steps[i];

    if (s.type === 'enterNode') {
      nodeStack.push(s.nodeType);
    } else if (s.type === 'exitNode') {
      nodeStack.pop();
    } else if (s.type === 'emitInstruction' && s.instrIndex !== undefined) {
      // Only care about jump instructions emitted as placeholders
      if (!allPatchTargets.has(s.instrIndex)) continue;

      const innermost = nodeStack[nodeStack.length - 1] || '';

      // Determine the variable name based on node context and opcode
      let varName = '';
      if (s.desc.includes('JumpIfFalse')) {
        if (innermost === 'IfStmt') {
          varName = 'jump_else';
        } else if (innermost === 'WhileStmt' || innermost === 'ForStmt') {
          varName = 'exit_jump';
        } else if (nodeStack.includes('LogicalExpr')) {
          varName = 'skip_jump';
        }
      } else if (s.desc.includes('JumpIfTrue')) {
        if (nodeStack.includes('LogicalExpr')) {
          varName = 'skip_jump';
        }
      } else if (s.desc.includes('Jump')) {
        if (innermost === 'IfStmt') {
          varName = 'jump_end';
        } else if (innermost === 'BreakStmt') {
          // break jumps go into break_jumps vector — shown in loop context
          continue;
        } else if (innermost === 'ContinueStmt') {
          // continue jumps go into continue_jumps — shown in loop context
          continue;
        }
      }

      if (!varName) continue;

      // Check if there's already an unpatched var with this name for this node —
      // that would mean we're in a nested structure and need a unique name
      const existing = vars.find(v => v.name === varName && v.nodeType === innermost && !v.patched);
      if (existing) {
        // Nested same-type: add depth marker
        const depth = vars.filter(v => v.name.startsWith(varName) && !v.patched).length;
        varName = varName + ' (' + (depth + 1) + ')';
      }

      vars.push({
        name: varName,
        instrIndex: s.instrIndex,
        nodeType: innermost,
        patched: false,
      });
    } else if (s.type === 'patchJump' && s.instrIndex !== undefined) {
      // Mark the matching variable as patched
      // Find the most recent unpatched var matching this instruction index
      for (let j = vars.length - 1; j >= 0; j--) {
        if (vars[j].instrIndex === s.instrIndex && !vars[j].patched) {
          vars[j].patched = true;
          vars[j].patchTarget = s.patchTarget;
          break;
        }
      }
    }
  }

  return vars;
}

function renderJumpVars(state: ExampleState): void {
  if (!state.traceData) {
    state.jumpVarsEl.innerHTML = '';
    return;
  }

  const vars = reconstructJumpVars(state.traceData.steps, state.stepIndex);

  // Only show variables that are currently live (unpatched) or were just patched
  const step = state.traceData.steps[state.stepIndex];
  const justPatchedIdx = step.type === 'patchJump' ? step.instrIndex : -1;

  // Show unpatched vars, plus any that were patched on this very step
  const visible = vars.filter(v => !v.patched || v.instrIndex === justPatchedIdx);

  if (visible.length === 0) {
    state.jumpVarsEl.innerHTML = '<span class="cf-empty">no saved jumps</span>';
    return;
  }

  let html = '';
  for (const v of visible) {
    const addr = '[' + String(v.instrIndex).padStart(4, '0') + ']';
    let cls = 'cf-jumpvar';
    let valueHtml = '';

    if (v.patched) {
      cls += ' cf-jumpvar-patched';
      valueHtml = '<span class="cf-jumpvar-value cf-field-value-set">' + addr +
        ' \u2192 [' + String(v.patchTarget).padStart(4, '0') + '] (patched)</span>';
    } else {
      cls += ' cf-jumpvar-live';
      valueHtml = '<span class="cf-jumpvar-value cf-field-value-unknown">' + addr +
        ' (waiting)</span>';
    }

    html += '<div class="' + cls + '">' +
      '<span class="cf-jumpvar-name">' + escapeHtml(v.name) + '</span>' +
      valueHtml +
      '</div>';
  }

  state.jumpVarsEl.innerHTML = html;
}

function renderSourceHighlight(state: ExampleState): void {
  if (!state.traceData) {
    state.sourceOverlayEl.classList.add('hidden');
    return;
  }
  const data = state.traceData;

  // Build node stack and find innermost node with a line
  const nodeStack: { nodeType: string; line?: number; col?: number }[] = [];
  for (let i = 0; i <= state.stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'enterNode') {
      nodeStack.push({ nodeType: s.nodeType, line: s.line, col: s.col });
    } else if (s.type === 'exitNode') {
      nodeStack.pop();
    }
  }

  // Find innermost node with a valid line
  let activeLine = -1;
  for (let i = nodeStack.length - 1; i >= 0; i--) {
    if (nodeStack[i].line !== undefined && nodeStack[i].line! > 0) {
      activeLine = nodeStack[i].line!;
      break;
    }
  }

  const source = state.editorEl.value;
  const lines = source.split('\n');

  let html = '';
  for (let i = 0; i < lines.length; i++) {
    const lineNum = i + 1;
    const isActive = lineNum === activeLine;
    const cls = 'cf-source-line' + (isActive ? ' cf-source-line-active' : '');
    html += '<div class="' + cls + '">' +
      '<span class="cf-source-line-number">' + lineNum + '</span>' +
      '<span class="cf-source-line-text">' + escapeHtml(lines[i]) + '</span>' +
      '</div>';
  }

  state.sourceOverlayEl.innerHTML = html;
  state.sourceOverlayEl.classList.remove('hidden');
  state.editorEl.classList.add('hidden');

  // Scroll active line into view (skip during initial auto-compile)
  if (!initializing) {
    const activeEl = state.sourceOverlayEl.querySelector('.cf-source-line-active');
    if (activeEl) {
      activeEl.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

// --- Build the page ---

function buildExampleCard(
  example: { title: string; source: string; description: string },
): ExampleState {
  const editorEl = document.createElement('textarea');
  editorEl.className = 'cf-editor';
  editorEl.spellcheck = false;
  editorEl.setAttribute('autocorrect', 'off');
  editorEl.setAttribute('autocapitalize', 'off');
  editorEl.value = example.source;

  // Tab support in editor
  editorEl.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.key === 'Tab') {
      e.preventDefault();
      const start = editorEl.selectionStart;
      const end = editorEl.selectionEnd;
      editorEl.value =
        editorEl.value.substring(0, start) + '  ' + editorEl.value.substring(end);
      editorEl.selectionStart = editorEl.selectionEnd = start + 2;
    }
  });

  const sourceOverlayEl = el('pre', { className: 'cf-source-overlay hidden' }) as HTMLElement;

  const compileBtn = el('button', { className: 'btn btn-primary cf-compile-btn' }, 'Compile & Step');

  const stepCounterEl = el('span', { className: 'step-counter' });
  const resetBtn = el('button', { disabled: 'true' }, '\u23EE');
  const prevBtn = el('button', { disabled: 'true' }, '\u25C0') as HTMLButtonElement;
  const nextBtn = el('button', { disabled: 'true' }, '\u25B6') as HTMLButtonElement;
  const autoBtn = el('button', { className: 'step-auto-btn' }, '\u25B6');

  const stepControlsEl = el('div', { className: 'step-controls cf-step-controls' },
    resetBtn, prevBtn, nextBtn, autoBtn, stepCounterEl,
  );

  const stepInfoEl = el('div', { className: 'step-info cf-step-info' });
  const patchAnnotationEl = el('div', { className: 'cf-patch-annotation hidden' }) as HTMLElement;

  const nodeStackEl = el('div', { className: 'cf-node-stack' });
  const nodeStackSection = el('div', { className: 'cf-section' },
    el('div', { className: 'cf-section-header' }, 'AST Node Stack'),
    nodeStackEl,
  );

  const jumpVarsEl = el('div', { className: 'cf-jumpvars-panel' }) as HTMLElement;
  const jumpVarsSection = el('div', { className: 'cf-section' },
    el('div', { className: 'cf-section-header' }, 'Saved Jump Offsets'),
    jumpVarsEl,
  );

  const bytecodeEl = el('div', { className: 'cf-bytecode-panel' });
  const bytecodeSection = el('div', { className: 'cf-section cf-section-bytecode' },
    el('div', { className: 'cf-section-header' }, 'Bytecode'),
    bytecodeEl,
  );

  const loopContextEl = el('div', { className: 'cf-loop-panel' });
  const loopContextSection = el('div', { className: 'cf-section cf-section-loop' },
    el('div', { className: 'cf-section-header' }, 'LoopContext Stack'),
    loopContextEl,
  );

  const descEl = el('div', { className: 'cf-description' }, example.description);

  const containerEl = el('div', { className: 'cf-card' },
    el('div', { className: 'cf-card-header' },
      el('h2', {}, example.title),
      compileBtn,
    ),
    descEl,
    el('div', { className: 'cf-card-body' },
      el('div', { className: 'cf-editor-col' },
        el('label', {}, 'Source'),
        editorEl,
        sourceOverlayEl,
      ),
      el('div', { className: 'cf-output-col' },
        stepControlsEl,
        stepInfoEl,
        patchAnnotationEl,
        nodeStackSection,
        jumpVarsSection,
        el('div', { className: 'cf-output-split' },
          bytecodeSection,
          loopContextSection,
        ),
      ),
    ),
  );

  const state: ExampleState = {
    title: example.title,
    source: example.source,
    description: example.description,
    traceData: null,
    stepIndex: 0,
    autoPlayTimer: null,
    editorEl,
    sourceOverlayEl,
    stepInfoEl,
    patchAnnotationEl,
    jumpVarsEl,
    stepCounterEl,
    bytecodeEl,
    loopContextEl,
    nodeStackEl,
    prevBtn: prevBtn as HTMLButtonElement,
    nextBtn: nextBtn as HTMLButtonElement,
    autoBtn: autoBtn as HTMLButtonElement,
    resetBtn: resetBtn as HTMLButtonElement,
    compileBtn: compileBtn as HTMLButtonElement,
    containerEl,
  };

  // Wire events
  compileBtn.addEventListener('click', () => compileExample(state));
  nextBtn.addEventListener('click', () => stepNext(state));
  prevBtn.addEventListener('click', () => stepPrev(state));
  resetBtn.addEventListener('click', () => stepReset(state));
  autoBtn.addEventListener('click', () => toggleAutoPlay(state));

  return state;
}

function buildPage(): void {
  const root = document.getElementById('app')!;

  const nav = el('nav', { className: 'app-nav' },
    el('a', { href: '../' }, 'Home'),
    el('a', { href: '../playground/' }, 'Playground'),
    el('a', { href: '../test262/' }, 'Test262 Runner'),
    el('a', { href: '../custom/' }, 'Custom Tests'),
  );

  const header = el('header', { className: 'app-header' },
    el('h1', {}, 'Yatsi'),
    el('p', { className: 'subtitle' }, 'Control Flow Compilation'),
    nav,
  );

  const intro = el('div', { className: 'cf-intro' },
    el('p', {},
      'Walk through how the compiler handles control flow constructs. ' +
      'Each example shows the bytecode being built instruction by instruction, ' +
      'with jump patching and loop context state visible at every step.',
    ),
    el('div', { className: 'cf-legend' },
      el('span', { className: 'cf-legend-item' },
        el('span', { className: 'cf-legend-swatch cf-swatch-emit' }),
        ' emitted',
      ),
      el('span', { className: 'cf-legend-item' },
        el('span', { className: 'cf-legend-swatch cf-swatch-patch' }),
        ' patched',
      ),
      el('span', { className: 'cf-legend-item' },
        el('span', { className: 'cf-legend-swatch cf-swatch-target' }),
        ' jump target',
      ),
    ),
  );

  root.appendChild(header);
  root.appendChild(intro);

  for (const example of EXAMPLES) {
    const state = buildExampleCard(example);
    states.push(state);
    root.appendChild(state.containerEl);
  }

  root.appendChild(siteFooter());

  // Keyboard shortcuts: arrow keys step the focused card
  document.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.target instanceof HTMLTextAreaElement) return;

    // Find which card is "active" (last interacted)
    const activeState = states.find(s => s.containerEl.contains(document.activeElement)) ||
      states.find(s => s.traceData !== null);
    if (!activeState || !activeState.traceData) return;

    switch (e.key) {
      case 'ArrowRight':
      case 'Enter':
        e.preventDefault();
        stepNext(activeState);
        break;
      case 'ArrowLeft':
        e.preventDefault();
        stepPrev(activeState);
        break;
      case 'Home':
        e.preventDefault();
        stepReset(activeState);
        break;
      case ' ':
        e.preventDefault();
        toggleAutoPlay(activeState);
        break;
    }
  });
}

// --- Init ---

async function init(): Promise<void> {
  buildPage();
  engine = await YatsiEngine.create();

  // Auto-compile all examples on load
  for (const state of states) {
    compileExample(state);
  }
  initializing = false;
}

init();
