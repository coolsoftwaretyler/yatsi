import { el, siteFooter } from './ui/components';
import { YatsiEngine } from './engine/wasm-engine';

// --- Types ---

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
  functionName?: string;
  variableName?: string;
  upvalueIndex?: number;
  isLocalUpvalue?: boolean;
  paramCount?: number;
  upvalueCount?: number;
}

interface CompilerInstruction {
  opcode: string;
  a: number;
  b: number;
  c: number;
  bx: number;
  sbx: number;
}

interface UpvalueDesc {
  index: number;
  isLocal: boolean;
}

interface ChildFunction {
  name: string;
  paramCount: number;
  registerCount: number;
  instructions: CompilerInstruction[];
  constants: string[];
  upvalueDescs: UpvalueDesc[];
  functions: ChildFunction[];
}

interface CompilerTraceResult {
  steps: CompilerStep[];
  instructions: CompilerInstruction[];
  constants: string[];
  registerCount: number;
  functions: ChildFunction[];
  ast: string;
  bytecode: string;
  errors: string[];
}

// --- VM Runtime Types ---

interface VMRegWrite { index: number; value: string; }

interface VMStep {
  type: 'execute' | 'call' | 'return' | 'captureUpvalue' |
        'closeUpvalue' | 'readUpvalue' | 'writeUpvalue';
  opcode: string;
  a: number; b: number; c: number;
  bx: number; sbx: number;
  ip: number;
  functionName: string;
  callDepth: number;
  baseRegister: number;
  desc: string;
  regWrites?: VMRegWrite[];
  upvalueIndex?: number;
  upvalueVarName?: string;
  upvalueIsOpen?: boolean;
  upvalueValue?: string;
  closureFuncIndex?: number;
  upvalueCount?: number;
}

interface VMTraceProgram {
  instructions: CompilerInstruction[];
  constants: string[];
  registerCount: number;
  functions: ChildFunction[];
  bytecode: string;
}

interface VMTraceResult {
  program: VMTraceProgram;
  steps: VMStep[];
  output: string;
  error: string | null;
}

// --- Reconstructed state types ---

interface ReconstructedLocal {
  name: string;
  register: number;
  depth: number;
  isCaptured: boolean;
}

interface ReconstructedUpvalue {
  index: number;
  sourceIndex: number;
  isLocal: boolean;
  name: string;
}

/** Snapshot of the compiler state saved in an EnclosingState when entering a child function. */
interface EnclosingStateSnapshot {
  functionName: string;  // name of the function whose state was saved (the parent)
  locals: ReconstructedLocal[];
  upvalues: ReconstructedUpvalue[];
  scopeDepth: number;
  // depth in the chain: 0 = outermost saved state (<script>), 1 = next level, etc.
  chainDepth: number;
}

// --- Example source snippets ---

const EXAMPLES: { title: string; source: string; description: string }[] = [
  {
    title: 'Basic Closure (counter)',
    source: `function makeCounter() {
  let count = 0;
  return () => {
    count = count + 1;
    return count;
  };
}`,
    description:
      'A function returns an arrow function that captures the local variable count. ' +
      'The compiler marks count as captured, creates an upvalue descriptor, and emits ' +
      'Closure, GetUpvalue, and SetUpvalue instructions.',
  },
  {
    title: 'Parameter Capture (adder)',
    source: `function makeAdder(x) {
  return (y) => x + y;
}`,
    description:
      'The arrow function captures the parameter x from its enclosing function. ' +
      'Parameters are locals in the enclosing scope, so the upvalue is resolved ' +
      'with is_local=true, pointing at the register holding x.',
  },
  {
    title: 'Multiple Captures',
    source: `function makeGreeter(greeting) {
  let count = 0;
  return (name) => {
    count = count + 1;
    return greeting;
  };
}`,
    description:
      'The arrow captures both a parameter (greeting) and a local (count). ' +
      'Each gets its own upvalue descriptor. Watch the upvalue table grow as ' +
      'each variable is resolved during arrow function compilation.',
  },
  {
    title: 'Shared Closures',
    source: `function makeShared() {
  let value = 0;
  function increment() {
    value = value + 1;
    return value;
  }
  function decrement() {
    value = value - 1;
    return value;
  }
  return increment;
}`,
    description:
      'Two named functions both capture the same variable value. Each function ' +
      'gets its own upvalue descriptor pointing to the same local register. ' +
      'The MarkCaptured step fires when the first function resolves the upvalue.',
  },
  {
    title: 'Nested Closures (2 levels)',
    source: `function outer() {
  let x = 10;
  function middle() {
    let y = 20;
    return () => x + y;
  }
  return middle;
}`,
    description:
      'The arrow function captures y from middle (is_local=true) and x from outer ' +
      '(is_local=false — chained through middle\'s upvalue). Watch the scope stack ' +
      'grow to depth 3 and the chained upvalue resolution.',
  },
  {
    title: 'Closure + Mutation',
    source: `function makeCounter() {
  let n = 0;
  return () => {
    n = n + 1;
    return n;
  };
}
let counter = makeCounter();
console.log(counter());
console.log(counter());`,
    description:
      'The classic counter pattern. The arrow captures n and mutates it via SetUpvalue. ' +
      'Each call to counter() reads n with GetUpvalue, increments, and writes back with ' +
      'SetUpvalue. The variable lives on the heap as a closed-over upvalue.',
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
  // Runtime mode state
  viewMode: 'compile' | 'runtime';
  runtimeData: VMTraceResult | null;
  runtimeStepIndex: number;
  // Feature 1: Function Explorer
  contextTabMode: 'context' | 'explorer';
  functionExplorerEl: HTMLElement;
  // Feature 2: Source/AST toggle
  leftTabMode: 'source' | 'ast';
  astOverlayEl: HTMLElement;
  // Feature 3: Resolution chain
  resolutionChainEl: HTMLElement;
  // Feature 6: Interleaved bytecode
  bytecodeViewMode: 'perFunction' | 'interleaved';
  // DOM refs
  editorEl: HTMLTextAreaElement;
  sourceOverlayEl: HTMLElement;
  stepInfoEl: HTMLElement;
  annotationEl: HTMLElement;
  stepCounterEl: HTMLElement;
  bytecodeEl: HTMLElement;
  closureContextEl: HTMLElement;
  nodeStackEl: HTMLElement;
  prevBtn: HTMLButtonElement;
  nextBtn: HTMLButtonElement;
  autoBtn: HTMLButtonElement;
  resetBtn: HTMLButtonElement;
  compileBtn: HTMLButtonElement;
  containerEl: HTMLElement;
  modeToggleEl: HTMLElement;
  compileModeBtn: HTMLButtonElement;
  runtimeModeBtn: HTMLButtonElement;
  outputEl: HTMLElement;
}

let engine: YatsiEngine | null = null;
const states: ExampleState[] = [];
(window as any).__closureStates = states;
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

// --- Scope tracking ---

/** Replay EnterFunction/ExitFunction to build the scope chain at a given step. */
function reconstructScopeStack(
  steps: CompilerStep[],
  upToIndex: number,
): string[] {
  const stack: string[] = ['<script>'];
  for (let i = 0; i <= upToIndex; i++) {
    const s = steps[i];
    if (s.type === 'enterFunction' && s.functionName) {
      stack.push(s.functionName);
    } else if (s.type === 'exitFunction') {
      stack.pop();
    }
  }
  return stack;
}

/** Get which function depth we're in (0 = script, 1 = first child, etc.) */
function getCurrentFunctionDepth(
  steps: CompilerStep[],
  upToIndex: number,
): number {
  let depth = 0;
  for (let i = 0; i <= upToIndex; i++) {
    if (steps[i].type === 'enterFunction') depth++;
    else if (steps[i].type === 'exitFunction') depth--;
  }
  return depth;
}

/** Reconstruct the locals table for the current function scope. */
function reconstructLocalsTable(
  steps: CompilerStep[],
  upToIndex: number,
): ReconstructedLocal[] {
  // We need to track which function scope we're in.
  // Only show locals for the current (innermost) scope.
  let currentDepth = 0;
  // Track depth-scoped locals: when we enter a function, start fresh; on exit, discard
  const localsStack: ReconstructedLocal[][] = [[]];

  for (let i = 0; i <= upToIndex; i++) {
    const s = steps[i];
    if (s.type === 'enterFunction') {
      currentDepth++;
      localsStack.push([]);
    } else if (s.type === 'exitFunction') {
      localsStack.pop();
      currentDepth--;
    } else if (s.type === 'enterNode' && s.nodeType === 'VarDeclaration' && s.desc) {
      // Look ahead to find the register allocated for this variable
      // within the same function depth
      const varName = s.desc;
      let reg = -1;
      for (let j = i + 1; j <= upToIndex && j < steps.length; j++) {
        if (steps[j].type === 'exitNode' && steps[j].nodeType === 'VarDeclaration') break;
        if (steps[j].type === 'allocRegister' && steps[j].registerId !== undefined) {
          reg = steps[j].registerId!;
          break;
        }
      }
      if (reg >= 0 && localsStack.length > 0) {
        // scope_depth: 0 at script level, 1 inside function body
        localsStack[localsStack.length - 1].push({
          name: varName,
          register: reg,
          depth: currentDepth === 0 ? 0 : 1,
          isCaptured: false,
        });
      }
    } else if (s.type === 'allocRegister' && localsStack.length > 0) {
      // Check if we're inside a param registration (FunctionDecl or ArrowFunction params)
      // by looking at the node stack
      const nodeStack: string[] = [];
      for (let j = 0; j <= i; j++) {
        if (steps[j].type === 'enterNode') nodeStack.push(steps[j].nodeType);
        else if (steps[j].type === 'exitNode') nodeStack.pop();
      }
      // Params are registered as locals but via allocRegister inside the function body.
      // They don't have VarDeclaration nodes. We detect them by looking for allocRegister
      // immediately after enterFunction without an intervening node entry.
    } else if (s.type === 'markCaptured' && s.variableName) {
      // Mark the local as captured in the enclosing scope
      // MarkCaptured fires in the child function but targets the enclosing function's locals
      if (localsStack.length >= 2) {
        const enclosingLocals = localsStack[localsStack.length - 2];
        for (const local of enclosingLocals) {
          if (local.name === s.variableName) {
            local.isCaptured = true;
          }
        }
      }
    }
  }

  // Return locals for the current (innermost) function scope
  return localsStack.length > 0 ? localsStack[localsStack.length - 1] : [];
}

/** Reconstruct upvalue table for the current function scope. */
function reconstructUpvalueTable(
  steps: CompilerStep[],
  upToIndex: number,
): ReconstructedUpvalue[] {
  let depth = 0;
  // Track per-function-depth upvalues
  const upvalueStacks: ReconstructedUpvalue[][] = [[]];

  for (let i = 0; i <= upToIndex; i++) {
    const s = steps[i];
    if (s.type === 'enterFunction') {
      depth++;
      upvalueStacks.push([]);
    } else if (s.type === 'exitFunction') {
      upvalueStacks.pop();
      depth--;
    } else if (s.type === 'addUpvalue' && s.upvalueIndex !== undefined) {
      if (upvalueStacks.length > 0) {
        // Find the variable name from the most recent resolveUpvalue step
        let varName = '';
        for (let j = i + 1; j <= upToIndex; j++) {
          if (steps[j].type === 'resolveUpvalue' && steps[j].variableName) {
            varName = steps[j].variableName!;
            break;
          }
        }
        // If not found looking ahead, look backwards
        if (!varName) {
          for (let j = i - 1; j >= 0; j--) {
            if (steps[j].type === 'resolveUpvalue' && steps[j].variableName) {
              varName = steps[j].variableName!;
              break;
            }
            if (steps[j].type === 'enterFunction' || steps[j].type === 'exitFunction') break;
          }
        }

        upvalueStacks[upvalueStacks.length - 1].push({
          index: s.upvalueIndex,
          sourceIndex: -1, // filled from resolveUpvalue
          isLocal: s.isLocalUpvalue || false,
          name: varName,
        });
      }
    } else if (s.type === 'resolveUpvalue' && s.upvalueIndex !== undefined && upvalueStacks.length > 0) {
      const current = upvalueStacks[upvalueStacks.length - 1];
      // Update the matching upvalue entry with source info
      for (const uv of current) {
        if (uv.index === s.upvalueIndex && s.variableName) {
          uv.name = s.variableName;
          uv.isLocal = s.isLocalUpvalue || false;
        }
      }
    }
  }

  return upvalueStacks.length > 0 ? upvalueStacks[upvalueStacks.length - 1] : [];
}

/**
 * Reconstruct the chain of EnclosingState objects that exist at the current step.
 * Each enterFunction saves the parent's locals, upvalues, and scope depth.
 * Returns the chain from outermost (first saved) to innermost (most recently saved).
 */
function reconstructEnclosingStates(
  steps: CompilerStep[],
  upToIndex: number,
): EnclosingStateSnapshot[] {
  // We replay the trace, maintaining a stack of saved states.
  // At each enterFunction, we snapshot the current locals/upvalues and push onto the chain.
  // At each exitFunction, we pop from the chain (the state was restored).

  // Current compiler state we're tracking as we replay
  const localsStack: ReconstructedLocal[][] = [[]];
  const upvalueStacks: ReconstructedUpvalue[][] = [[]];
  const scopeDepths: number[] = [0];
  const funcNames: string[] = ['<script>'];

  // The saved states (the EnclosingState linked list)
  const chain: EnclosingStateSnapshot[] = [];

  for (let i = 0; i <= upToIndex; i++) {
    const s = steps[i];

    // Track locals being added to the current scope
    if (s.type === 'enterNode' && s.nodeType === 'VarDeclaration' && s.desc) {
      const varName = s.desc;
      let reg = -1;
      for (let j = i + 1; j <= upToIndex && j < steps.length; j++) {
        if (steps[j].type === 'exitNode' && steps[j].nodeType === 'VarDeclaration') break;
        if (steps[j].type === 'allocRegister' && steps[j].registerId !== undefined) {
          reg = steps[j].registerId!;
          break;
        }
      }
      if (reg >= 0 && localsStack.length > 0) {
        const currentScopeDepth = scopeDepths[scopeDepths.length - 1];
        localsStack[localsStack.length - 1].push({
          name: varName,
          register: reg,
          depth: currentScopeDepth,
          isCaptured: false,
        });
      }
    }

    // Track captured status — update both the live localsStack AND the chain snapshot
    if (s.type === 'markCaptured' && s.variableName) {
      if (localsStack.length >= 2) {
        const enclosingLocals = localsStack[localsStack.length - 2];
        for (const local of enclosingLocals) {
          if (local.name === s.variableName) {
            local.isCaptured = true;
          }
        }
      }
      // Also update the most recent chain entry (the saved EnclosingState for the parent)
      if (chain.length > 0) {
        const parentState = chain[chain.length - 1];
        for (const local of parentState.locals) {
          if (local.name === s.variableName) {
            local.isCaptured = true;
          }
        }
      }
    }

    // Track upvalues being added
    if (s.type === 'addUpvalue' && s.upvalueIndex !== undefined && upvalueStacks.length > 0) {
      let varName = '';
      for (let j = i + 1; j <= upToIndex; j++) {
        if (steps[j].type === 'resolveUpvalue' && steps[j].variableName) {
          varName = steps[j].variableName!;
          break;
        }
      }
      if (!varName) {
        for (let j = i - 1; j >= 0; j--) {
          if (steps[j].type === 'resolveUpvalue' && steps[j].variableName) {
            varName = steps[j].variableName!;
            break;
          }
          if (steps[j].type === 'enterFunction' || steps[j].type === 'exitFunction') break;
        }
      }
      upvalueStacks[upvalueStacks.length - 1].push({
        index: s.upvalueIndex,
        sourceIndex: -1,
        isLocal: s.isLocalUpvalue || false,
        name: varName,
      });
    }

    if (s.type === 'resolveUpvalue' && s.upvalueIndex !== undefined && upvalueStacks.length > 0) {
      const current = upvalueStacks[upvalueStacks.length - 1];
      for (const uv of current) {
        if (uv.index === s.upvalueIndex && s.variableName) {
          uv.name = s.variableName;
          uv.isLocal = s.isLocalUpvalue || false;
        }
      }
    }

    // On enterFunction: snapshot the current state as an EnclosingState, then start fresh
    if (s.type === 'enterFunction') {
      // The current (parent) state is being saved
      const savedLocals = localsStack[localsStack.length - 1].map(l => ({ ...l }));
      const savedUpvalues = upvalueStacks[upvalueStacks.length - 1].map(u => ({ ...u }));
      const savedScopeDepth = scopeDepths[scopeDepths.length - 1];
      const savedFuncName = funcNames[funcNames.length - 1];

      chain.push({
        functionName: savedFuncName,
        locals: savedLocals,
        upvalues: savedUpvalues,
        scopeDepth: savedScopeDepth,
        chainDepth: chain.length,
      });

      // Start fresh for the child function
      localsStack.push([]);
      upvalueStacks.push([]);
      scopeDepths.push(1);
      funcNames.push(s.functionName || '?');
    }

    // On exitFunction: pop the saved state (compiler restores from EnclosingState)
    if (s.type === 'exitFunction') {
      localsStack.pop();
      upvalueStacks.pop();
      scopeDepths.pop();
      funcNames.pop();
      chain.pop();
    }
  }

  return chain;
}

// --- Flat function collection for full bytecode dump ---

interface FlatFunctionInfo {
  name: string;
  instructions: CompilerInstruction[];
  constants: string[];
  registerCount: number;
  upvalueDescs: UpvalueDesc[];
  /** 0 = script, 1 = direct child, etc. */
  depth: number;
  /** Index of this function's enterFunction step (-1 for script) */
  enterStepIndex: number;
  /** Index of this function's exitFunction step (-1 if not yet exited) */
  exitStepIndex: number;
  /** Child function index in the tree at the parent level */
  childIndex: number;
  /** Direct child functions of this function */
  childFunctions: ChildFunction[];
}

/**
 * Pre-order traversal of the function tree to produce a flat list.
 * The top-level script comes first, then each child recursively.
 */
function collectAllFunctions(data: CompilerTraceResult): FlatFunctionInfo[] {
  const result: FlatFunctionInfo[] = [];

  // Script level (top-level)
  result.push({
    name: '<script>',
    instructions: data.instructions,
    constants: data.constants,
    registerCount: data.registerCount,
    upvalueDescs: [],
    depth: 0,
    enterStepIndex: -1,
    exitStepIndex: -1,
    childIndex: -1,
    childFunctions: data.functions,
  });

  // Find enter/exit step indices for each function
  // We walk the steps to map child functions to their enter/exit indices
  const enterExitPairs: { enterIdx: number; exitIdx: number; depth: number; childPath: number[] }[] = [];
  const depthStack: number[] = [];
  const childCountStack: number[] = [0];

  for (let i = 0; i < data.steps.length; i++) {
    const s = data.steps[i];
    if (s.type === 'enterFunction') {
      childCountStack[childCountStack.length - 1]++;
      depthStack.push(i);
      childCountStack.push(0);
      enterExitPairs.push({ enterIdx: i, exitIdx: -1, depth: depthStack.length, childPath: [] });
    } else if (s.type === 'exitFunction') {
      const enterIdx = depthStack.pop()!;
      childCountStack.pop();
      // Find the matching pair
      for (const pair of enterExitPairs) {
        if (pair.enterIdx === enterIdx) {
          pair.exitIdx = i;
          break;
        }
      }
    }
  }

  // Recursive walk of the ChildFunction tree
  function walkChildren(children: ChildFunction[], parentDepth: number, pathPrefix: number[]): void {
    for (let ci = 0; ci < children.length; ci++) {
      const child = children[ci];
      const funcDepth = parentDepth + 1;

      // Find the enter/exit pair for this child by matching against the traversal order
      let enterIdx = -1;
      let exitIdx = -1;
      // We assigned pairs in order of enterFunction steps, which matches pre-order
      if (pairCursor < enterExitPairs.length) {
        enterIdx = enterExitPairs[pairCursor].enterIdx;
        exitIdx = enterExitPairs[pairCursor].exitIdx;
        pairCursor++;
      }

      result.push({
        name: child.name,
        instructions: child.instructions,
        constants: child.constants,
        registerCount: child.registerCount,
        upvalueDescs: child.upvalueDescs,
        depth: funcDepth,
        enterStepIndex: enterIdx,
        exitStepIndex: exitIdx,
        childIndex: ci,
        childFunctions: child.functions,
      });

      walkChildren(child.functions, funcDepth, [...pathPrefix, ci]);
    }
  }

  let pairCursor = 0;
  walkChildren(data.functions, 0, []);

  return result;
}

/**
 * Build upvalue name map by cross-referencing UpvalueDescs with trace resolveUpvalue steps.
 * Returns a map from UV index to variable name for a given function's enter/exit step range.
 */
function buildUpvalueNameMap(
  steps: CompilerStep[],
  enterStepIndex: number,
  exitStepIndex: number,
): Map<number, string> {
  const names = new Map<number, string>();
  const start = enterStepIndex >= 0 ? enterStepIndex : 0;
  const end = exitStepIndex >= 0 ? exitStepIndex : steps.length - 1;

  // Track function depth to only look at resolveUpvalue in the immediate child scope
  let depth = 0;
  for (let i = start; i <= end; i++) {
    const s = steps[i];
    if (s.type === 'enterFunction' && i !== enterStepIndex) depth++;
    else if (s.type === 'exitFunction' && i !== exitStepIndex) depth--;

    if (depth === 0 && s.type === 'resolveUpvalue' && s.upvalueIndex !== undefined && s.variableName) {
      names.set(s.upvalueIndex, s.variableName);
    }
    if (depth === 0 && s.type === 'addUpvalue' && s.upvalueIndex !== undefined) {
      // Also check for nearby variable names
      let varName = '';
      for (let j = i + 1; j <= end; j++) {
        if (steps[j].type === 'resolveUpvalue' && steps[j].variableName) {
          varName = steps[j].variableName!;
          break;
        }
        if (steps[j].type === 'enterFunction' || steps[j].type === 'exitFunction') break;
      }
      if (!varName) {
        for (let j = i - 1; j >= start; j--) {
          if (steps[j].type === 'resolveUpvalue' && steps[j].variableName) {
            varName = steps[j].variableName!;
            break;
          }
          if (steps[j].type === 'enterFunction' || steps[j].type === 'exitFunction') break;
        }
      }
      if (varName && !names.has(s.upvalueIndex!)) {
        names.set(s.upvalueIndex!, varName);
      }
    }
  }
  return names;
}

// --- Compile an example ---

function compileExample(state: ExampleState): void {
  if (!engine) return;

  const source = state.editorEl.value;
  if (source.trim() === '') return;

  // Compile trace (compile-time view)
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

  // Execute trace (runtime view)
  try {
    const rtJson = engine.executeTraced(source);
    state.runtimeData = JSON.parse(rtJson);
  } catch (_e) {
    state.runtimeData = null;
  }

  state.stepIndex = 0;
  state.runtimeStepIndex = 0;
  state.containerEl.classList.add('cf-active');
  state.compileBtn.textContent = 'Re-compile';
  updateModeButtons(state);
  renderView(state);
}

// --- Mode switching ---

function switchMode(state: ExampleState, mode: 'compile' | 'runtime'): void {
  if (state.viewMode === mode) return;
  stopAutoPlay(state);
  state.viewMode = mode;
  updateModeButtons(state);
  renderView(state);
}

function updateModeButtons(state: ExampleState): void {
  if (state.viewMode === 'compile') {
    state.compileModeBtn.classList.add('active');
    state.runtimeModeBtn.classList.remove('active');
  } else {
    state.compileModeBtn.classList.remove('active');
    state.runtimeModeBtn.classList.add('active');
  }
  // Show/hide runtime-only elements
  const hasRuntime = state.runtimeData && state.runtimeData.steps.length > 0;
  state.runtimeModeBtn.disabled = !hasRuntime;
  // Show/hide output panel
  const showOutput = state.viewMode === 'runtime';
  state.outputEl.classList.toggle('hidden', !showOutput);
  const outputSection = state.outputEl.parentElement;
  if (outputSection) outputSection.classList.toggle('hidden', !showOutput);
}

// --- Step controls ---

function getMaxSteps(state: ExampleState): number {
  if (state.viewMode === 'runtime') {
    return state.runtimeData ? state.runtimeData.steps.length : 0;
  }
  return state.traceData ? state.traceData.steps.length : 0;
}

function getCurrentStepIndex(state: ExampleState): number {
  return state.viewMode === 'runtime' ? state.runtimeStepIndex : state.stepIndex;
}

function setCurrentStepIndex(state: ExampleState, idx: number): void {
  if (state.viewMode === 'runtime') {
    state.runtimeStepIndex = idx;
  } else {
    state.stepIndex = idx;
  }
}

function stepNext(state: ExampleState): void {
  const max = getMaxSteps(state);
  if (max === 0) return;
  const cur = getCurrentStepIndex(state);
  if (cur < max - 1) {
    setCurrentStepIndex(state, cur + 1);
    renderView(state);
  } else {
    stopAutoPlay(state);
  }
}

function stepPrev(state: ExampleState): void {
  const max = getMaxSteps(state);
  if (max === 0) return;
  const cur = getCurrentStepIndex(state);
  if (cur > 0) {
    setCurrentStepIndex(state, cur - 1);
    renderView(state);
  }
}

function stepReset(state: ExampleState): void {
  const max = getMaxSteps(state);
  if (max === 0) return;
  stopAutoPlay(state);
  setCurrentStepIndex(state, 0);
  renderView(state);
}

function toggleAutoPlay(state: ExampleState): void {
  const max = getMaxSteps(state);
  if (max === 0) return;
  if (state.autoPlayTimer) {
    stopAutoPlay(state);
  } else {
    startAutoPlay(state);
  }
}

function startAutoPlay(state: ExampleState): void {
  const max = getMaxSteps(state);
  if (max === 0) return;
  if (getCurrentStepIndex(state) >= max - 1) {
    setCurrentStepIndex(state, 0);
    renderView(state);
  }
  state.autoBtn.innerHTML = '&#9646;&#9646;';
  state.autoBtn.classList.add('active');
  autoPlayTick(state);
}

function autoPlayTick(state: ExampleState): void {
  const max = getMaxSteps(state);
  const cur = getCurrentStepIndex(state);
  if (max === 0 || cur >= max - 1) { stopAutoPlay(state); return; }

  setCurrentStepIndex(state, cur + 1);
  renderView(state);

  let delay = 300;
  if (state.viewMode === 'compile' && state.traceData) {
    const step = state.traceData.steps[state.stepIndex];
    switch (step.type) {
      case 'enterNode': case 'exitNode': delay = 150; break;
      case 'allocRegister': delay = 200; break;
      case 'emitInstruction': delay = 400; break;
      case 'addConstant': delay = 350; break;
      case 'patchJump': delay = 600; break;
      case 'pushLoop': case 'popLoop': delay = 500; break;
      case 'enterFunction': case 'exitFunction': delay = 600; break;
      case 'resolveUpvalue': delay = 700; break;
      case 'markCaptured': delay = 700; break;
      case 'addUpvalue': delay = 500; break;
      case 'resolveLocal': delay = 200; break;
      case 'resolveLocalNotFound': delay = 200; break;
      case 'upvalueDedup': delay = 400; break;
    }
  } else if (state.viewMode === 'runtime' && state.runtimeData) {
    const step = state.runtimeData.steps[state.runtimeStepIndex];
    switch (step.type) {
      case 'execute': delay = 250; break;
      case 'call': case 'return': delay = 500; break;
      case 'captureUpvalue': delay = 600; break;
      case 'closeUpvalue': delay = 700; break;
      case 'readUpvalue': case 'writeUpvalue': delay = 500; break;
    }
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

function renderView(state: ExampleState): void {
  if (state.viewMode === 'runtime') {
    renderRuntimeState(state);
  } else {
    renderState(state);
  }
}

function renderState(state: ExampleState): void {
  if (!state.traceData) return;
  const data = state.traceData;
  const step = data.steps[state.stepIndex];

  state.stepCounterEl.textContent =
    (state.stepIndex + 1) + ' / ' + data.steps.length;

  renderStepInfo(state, step);
  renderClosureAnnotation(state);
  renderResolutionChain(state);
  renderNodeStack(state);
  renderBytecode(state);
  if (state.contextTabMode === 'explorer') {
    renderFunctionExplorer(state);
  } else {
    renderClosureContext(state);
  }
  renderSourceHighlight(state);
  if (state.leftTabMode === 'ast') {
    renderAstView(state);
    state.astOverlayEl.classList.remove('hidden');
    state.sourceOverlayEl.classList.add('hidden');
    state.editorEl.classList.add('hidden');
  } else {
    state.astOverlayEl.classList.add('hidden');
  }

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
    case 'enterFunction':    badgeClass += ' step-badge-enterFunction'; label = 'enter func'; break;
    case 'exitFunction':     badgeClass += ' step-badge-exitFunction'; label = 'exit func'; break;
    case 'resolveUpvalue':   badgeClass += ' step-badge-resolveUpvalue'; label = 'resolve uv'; break;
    case 'markCaptured':     badgeClass += ' step-badge-markCaptured'; label = 'captured'; break;
    case 'addUpvalue':       badgeClass += ' step-badge-addUpvalue'; label = 'add uv'; break;
    case 'resolveGlobal':    badgeClass += ' step-badge-resolveGlobal'; label = 'global'; break;
    case 'resolveLocal':     badgeClass += ' step-badge-resolveLocal'; label = 'local found'; break;
    case 'resolveLocalNotFound': badgeClass += ' step-badge-resolveLocalNotFound'; label = 'local miss'; break;
    case 'upvalueDedup':     badgeClass += ' step-badge-upvalueDedup'; label = 'uv dedup'; break;
  }

  const nodeTypeHtml = step.nodeType
    ? '<span class="step-rule">' + escapeHtml(step.nodeType) + '</span> '
    : '';

  let descText = step.desc;
  // Show function name for allocRegister steps
  if (step.type === 'allocRegister' && step.functionName) {
    descText += ' (in ' + step.functionName + ')';
  }
  // Show function name for upvalue-related steps
  if ((step.type === 'markCaptured' || step.type === 'addUpvalue' || step.type === 'resolveUpvalue' ||
       step.type === 'resolveLocal' || step.type === 'resolveLocalNotFound' || step.type === 'upvalueDedup') && step.functionName) {
    descText += ' in ' + step.functionName;
  }

  state.stepInfoEl.innerHTML =
    '<span class="' + badgeClass + '">' + escapeHtml(label) + '</span> ' +
    nodeTypeHtml +
    '<span class="step-desc">' + escapeHtml(descText) + '</span>';
}

/**
 * Walk backwards from currentIndex to find the enclosing Identifier name.
 * Tracks enterNode/exitNode depth to find the first unmatched enterNode(Identifier).
 * Also handles AssignmentExpr context.
 */
function findEnclosingIdentifierName(
  steps: CompilerStep[],
  currentIndex: number,
): string | null {
  let depth = 0;
  for (let i = currentIndex; i >= 0; i--) {
    const s = steps[i];
    if (s.type === 'exitNode') {
      depth++;
    } else if (s.type === 'enterNode') {
      if (depth > 0) {
        depth--;
      } else {
        // Unmatched enterNode — this is an enclosing node
        if (s.nodeType === 'Identifier') {
          return s.desc || null;
        }
        if (s.nodeType === 'AssignmentExpr') {
          // desc is the operator (e.g. "Equal"), not the variable name.
          // Scan forward for resolveUpvalue/markCaptured variableName or enterNode(Identifier).
          for (let j = i + 1; j <= currentIndex; j++) {
            const fwd = steps[j];
            if ((fwd.type === 'resolveUpvalue' || fwd.type === 'markCaptured') && fwd.variableName) {
              return fwd.variableName;
            }
            if (fwd.type === 'enterNode' && fwd.nodeType === 'Identifier' && fwd.desc) {
              return fwd.desc;
            }
          }
          return null;
        }
        // Keep looking — we might be inside a deeper node
      }
    }
  }
  return null;
}

/**
 * Walk backwards from currentIndex within the current Identifier scope
 * to find a recent resolveUpvalue step.
 */
function findRecentResolveUpvalue(
  steps: CompilerStep[],
  currentIndex: number,
): { upvalueIndex: number; isLocalUpvalue: boolean; variableName: string } | null {
  let depth = 0;
  for (let i = currentIndex - 1; i >= 0; i--) {
    const s = steps[i];
    if (s.type === 'exitNode') {
      depth++;
    } else if (s.type === 'enterNode') {
      if (depth > 0) {
        depth--;
      } else {
        // Hit the enclosing enterNode — stop searching
        break;
      }
    }
    if (depth === 0 && s.type === 'resolveUpvalue' && s.upvalueIndex !== undefined && s.variableName) {
      return {
        upvalueIndex: s.upvalueIndex,
        isLocalUpvalue: s.isLocalUpvalue || false,
        variableName: s.variableName,
      };
    }
  }
  return null;
}

/**
 * Given an exitNode(Identifier) index, check if this was a local resolution.
 * Local resolution means no emitInstruction/resolveUpvalue/markCaptured/addUpvalue
 * occurred between the matching enterNode(Identifier) and this exitNode.
 */
function checkLocalResolution(
  steps: CompilerStep[],
  exitNodeIndex: number,
): { variableName: string; register: number } | null {
  const exitStep = steps[exitNodeIndex];
  if (!exitStep || exitStep.type !== 'exitNode' || exitStep.nodeType !== 'Identifier') {
    return null;
  }

  // Find the matching enterNode(Identifier)
  let depth = 0;
  let enterIndex = -1;
  for (let i = exitNodeIndex - 1; i >= 0; i--) {
    const s = steps[i];
    if (s.type === 'exitNode') {
      depth++;
    } else if (s.type === 'enterNode') {
      if (depth > 0) {
        depth--;
      } else {
        if (s.nodeType === 'Identifier') {
          enterIndex = i;
        }
        break;
      }
    }
  }

  if (enterIndex < 0) return null;

  const varName = steps[enterIndex].desc || null;
  if (!varName) return null;

  // Check that no instruction-related steps occurred between enter and exit
  for (let i = enterIndex + 1; i < exitNodeIndex; i++) {
    const s = steps[i];
    if (
      s.type === 'emitInstruction' ||
      s.type === 'resolveUpvalue' ||
      s.type === 'markCaptured' ||
      s.type === 'addUpvalue'
    ) {
      return null;
    }
  }

  // Look up the variable in the locals table to find its register
  const locals = reconstructLocalsTable(steps, exitNodeIndex);
  for (const local of locals) {
    if (local.name === varName) {
      return { variableName: varName, register: local.register };
    }
  }

  // Variable may be a parameter (not tracked via VarDeclaration).
  // Still report local resolution, but without a register number.
  return { variableName: varName, register: -1 };
}

function renderClosureAnnotation(state: ExampleState): void {
  if (!state.traceData) {
    state.annotationEl.classList.add('hidden');
    return;
  }

  const steps = state.traceData.steps;
  const step = steps[state.stepIndex];
  const funcDepth = getCurrentFunctionDepth(steps, state.stepIndex);
  let annotation = '';
  let pathType: 'local' | 'upvalue' | 'global' | null = null;

  switch (step.type) {
    case 'enterFunction': {
      const name = step.functionName || '?';
      const params = step.paramCount !== undefined ? step.paramCount : '?';
      annotation = 'Entering compilation of function \'' + name + '\' with ' + params +
        ' parameter(s). The compiler saves its current state (locals, upvalues, scope depth) ' +
        'into EnclosingState and starts fresh for the child function.';
      pathType = 'upvalue';
      break;
    }
    case 'exitFunction': {
      const name = step.functionName || '?';
      const uvCount = step.upvalueCount !== undefined ? step.upvalueCount : '?';
      annotation = 'Finished compiling function \'' + name + '\'. ' +
        'The child has ' + uvCount + ' upvalue descriptor(s). ' +
        'The compiler restores the parent\'s state from EnclosingState.';
      pathType = 'upvalue';
      break;
    }
    case 'markCaptured': {
      const varName = step.variableName || '?';
      const reg = step.registerId !== undefined ? 'R' + step.registerId : '?';
      const funcName = step.functionName || 'the enclosing function';
      annotation = 'The variable \'' + varName + '\' (' + reg +
        ') in ' + funcName + ' is marked is_captured = true. ' +
        'At runtime, the VM will keep this variable on the heap instead of the stack, ' +
        'so the closure can access it after the enclosing function returns.';
      pathType = 'upvalue';
      break;
    }
    case 'addUpvalue': {
      const uvIdx = step.upvalueIndex !== undefined ? 'UV' + step.upvalueIndex : '?';
      const isLocal = step.isLocalUpvalue;
      const funcName = step.functionName || 'the current function';
      annotation = 'Adding upvalue descriptor ' + uvIdx + ' to ' + funcName + '. ' +
        (isLocal
          ? 'is_local=true means this upvalue captures directly from the enclosing function\'s register file.'
          : 'is_local=false means this upvalue chains through another upvalue in the enclosing function.');
      pathType = 'upvalue';
      break;
    }
    case 'resolveUpvalue': {
      const varName = step.variableName || '?';
      const uvIdx = step.upvalueIndex !== undefined ? 'UV' + step.upvalueIndex : '?';
      const isLocal = step.isLocalUpvalue;
      const funcName = step.functionName || '';
      const funcSuffix = funcName ? ' (in ' + funcName + ')' : '';
      annotation = 'Resolved variable \'' + varName + '\' as ' + uvIdx + funcSuffix + '. ' +
        (isLocal
          ? 'Found in the enclosing function\'s locals (is_local=true). ' +
            'The VM will capture this register directly.'
          : 'Not found in immediate enclosing locals — resolved through a chain of upvalues (is_local=false). ' +
            'The VM will follow the upvalue chain at runtime.');
      pathType = 'upvalue';
      break;
    }
    case 'resolveLocal': {
      const varName = step.variableName || '?';
      const reg = step.registerId !== undefined ? 'R' + step.registerId : '?';
      const funcName = step.functionName || 'the current function';
      annotation = 'resolve_local(\'' + varName + '\') found the variable in ' + funcName +
        '\'s locals at ' + reg + '. No upvalue or global lookup needed.';
      pathType = 'local';
      break;
    }
    case 'resolveLocalNotFound': {
      const varName = step.variableName || '?';
      const funcName = step.functionName || 'the current function';
      annotation = 'resolve_local(\'' + varName + '\') did not find the variable in ' + funcName +
        '\'s locals. The compiler will try upvalue resolution next, or fall back to global.';
      pathType = null;
      break;
    }
    case 'upvalueDedup': {
      const uvIdx = step.upvalueIndex !== undefined ? 'UV' + step.upvalueIndex : '?';
      const funcName = step.functionName || 'the current function';
      annotation = uvIdx + ' already exists in ' + funcName + '\'s upvalue table. ' +
        'Reusing the existing index instead of adding a duplicate descriptor.';
      pathType = 'upvalue';
      break;
    }
    case 'resolveGlobal': {
      const varName = step.variableName || '?';
      // Look backwards for a resolveLocalNotFound to build richer annotation
      let enriched = false;
      for (let j = state.stepIndex - 1; j >= 0; j--) {
        const prev = steps[j];
        if (prev.type === 'enterNode' || prev.type === 'exitNode') break;
        if (prev.type === 'resolveLocalNotFound' && prev.variableName === varName) {
          annotation = 'resolve_local(\'' + varName + '\') missed → resolve_upvalue() returned -1' +
            ' → resolving as global.';
          enriched = true;
          break;
        }
      }
      if (!enriched) {
        annotation = 'The compiler looked for \'' + varName + '\' in locals — not found. ' +
          'resolve_upvalue() returned -1 (not in any enclosing scope). ' +
          'The variable will be resolved as a global at runtime.';
      }
      pathType = 'global';
      break;
    }
    case 'emitInstruction': {
      const opcode = step.desc;
      if (opcode === 'GetUpvalue' || opcode === 'SetUpvalue') {
        const name = findEnclosingIdentifierName(steps, state.stepIndex);
        const resolved = findRecentResolveUpvalue(steps, state.stepIndex);
        const uvLabel = resolved ? 'UV' + resolved.upvalueIndex : '?';
        const isLocalLabel = resolved ? String(resolved.isLocalUpvalue) : '?';
        const action = opcode === 'GetUpvalue' ? 'load from' : 'write back to';
        annotation = 'The compiler looked for \'' + (name || '?') + '\' in locals \u2014 not found. ' +
          'resolve_upvalue() found it as ' + uvLabel + ' (is_local=' + isLocalLabel + '). ' +
          'Emitting ' + opcode + ' to ' + action + ' the closure\'s upvalue array.';
        pathType = 'upvalue';
      } else if (opcode === 'GetGlobal' && funcDepth > 0) {
        const name = findEnclosingIdentifierName(steps, state.stepIndex);
        annotation = 'The compiler looked for \'' + (name || '?') + '\' in locals \u2014 not found. ' +
          'resolve_upvalue() returned -1 (not in any enclosing scope). ' +
          'Falling back to GetGlobal.';
        pathType = 'global';
      } else if (opcode === 'SetGlobal' && funcDepth > 0) {
        const name = findEnclosingIdentifierName(steps, state.stepIndex);
        annotation = 'The compiler looked for \'' + (name || '?') + '\' in locals \u2014 not found. ' +
          'resolve_upvalue() returned -1 (not in any enclosing scope). ' +
          'Falling back to SetGlobal.';
        pathType = 'global';
      } else if (opcode === 'Closure') {
        annotation = 'Emitting Closure instruction \u2014 creates a runtime closure object for the compiled ' +
          'child function, binding its upvalue descriptors.';
        pathType = 'upvalue';
      }
      break;
    }
    case 'exitNode': {
      if (step.nodeType === 'Identifier' && funcDepth > 0) {
        const localResult = checkLocalResolution(steps, state.stepIndex);
        if (localResult) {
          const regStr = localResult.register >= 0 ? ' at R' + localResult.register : '';
          annotation = 'The identifier \'' + localResult.variableName + '\' was found in the current ' +
            'function\'s locals' + regStr + '. No instruction needed \u2014 the value ' +
            'is already in a register.';
          pathType = 'local';
        }
      }
      break;
    }
  }

  if (!annotation) {
    state.annotationEl.classList.add('hidden');
    state.annotationEl.className = 'cl-annotation hidden';
    return;
  }

  state.annotationEl.textContent = annotation;
  let cls = 'cl-annotation';
  if (pathType === 'local') cls += ' cl-annotation-local';
  else if (pathType === 'upvalue') cls += ' cl-annotation-upvalue';
  else if (pathType === 'global') cls += ' cl-annotation-global';
  state.annotationEl.className = cls;
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

  if (state.bytecodeViewMode === 'interleaved') {
    renderInterleavedBytecode(state);
    return;
  }

  const data = state.traceData;
  const currentFuncDepth = getCurrentFunctionDepth(data.steps, state.stepIndex);

  // Collect all functions into a flat list
  const allFuncs = collectAllFunctions(data);

  // Determine visibility: a function is visible if
  // - it's the script (always visible), OR
  // - its exitFunction step has been reached (compilation complete), OR
  // - it's currently being compiled (entered but not yet exited)
  // Also determine which function is "active" (currently being compiled)

  // Build a list of enter/exit events to determine which function depth we map to which flat index
  // For each function in allFuncs, check visibility
  const visibleFuncs: { func: FlatFunctionInfo; isActive: boolean; isComplete: boolean }[] = [];

  for (const func of allFuncs) {
    if (func.depth === 0) {
      // Script is always visible; it's active when currentFuncDepth === 0
      visibleFuncs.push({ func, isActive: currentFuncDepth === 0, isComplete: true });
      continue;
    }

    // For child functions: check if entered
    if (func.enterStepIndex >= 0 && func.enterStepIndex <= state.stepIndex) {
      const isExited = func.exitStepIndex >= 0 && func.exitStepIndex <= state.stepIndex;
      // Currently being compiled if entered but not exited
      const isBeingCompiled = !isExited;
      visibleFuncs.push({ func, isActive: isBeingCompiled, isComplete: isExited });
    }
    // Not yet entered — hidden
  }

  let html = '';

  for (const { func, isActive, isComplete } of visibleFuncs) {
    // For each function section, replay the trace to count instructions emitted so far
    const funcDepthLevel = func.depth;

    let instrCount = 0;
    // These track the step index that last set each "newest" value,
    // so highlighting only fires when the current step is exactly that step.
    let newestInstrIndex = -1;
    let newestInstrStepIdx = -1;
    let newestPatchIndex = -1;
    let newestPatchStepIdx = -1;
    let patchTarget = -1;
    let newestRegisterId = -1;
    let newestRegisterStepIdx = -1;
    let newestConstantIndex = -1;
    let newestConstantStepIdx = -1;
    const constantsSoFar: number[] = [];
    let registerCount = 0;
    const allPatches: { from: number; to: number }[] = [];

    // Always replay the trace incrementally — even for completed functions,
    // so bytecode appears as it was emitted during the step-through.
    // For completed functions, replay up to the current stepIndex (capped at exitStep).
    // For the active function, replay up to stepIndex.
    const replayEnd = (isComplete && func.exitStepIndex >= 0)
      ? Math.min(func.exitStepIndex, state.stepIndex)
      : state.stepIndex;

    {
      let scopeDepth = 0;

      for (let i = 0; i <= replayEnd; i++) {
        const s = data.steps[i];
        if (s.type === 'enterFunction') scopeDepth++;
        else if (s.type === 'exitFunction') scopeDepth--;

        if (scopeDepth !== funcDepthLevel) continue;

        if (s.type === 'emitInstruction' && s.instrIndex !== undefined) {
          instrCount = s.instrIndex + 1;
          newestInstrIndex = s.instrIndex;
          newestInstrStepIdx = i;
        }
        if (s.type === 'patchJump' && s.instrIndex !== undefined) {
          newestPatchIndex = s.instrIndex;
          newestPatchStepIdx = i;
          if (s.patchTarget !== undefined) {
            patchTarget = s.patchTarget;
            allPatches.push({ from: s.instrIndex, to: s.patchTarget });
          }
        }
        if (s.type === 'addConstant' && s.constantIndex !== undefined) {
          if (!constantsSoFar.includes(s.constantIndex)) {
            constantsSoFar.push(s.constantIndex);
          }
          newestConstantIndex = s.constantIndex;
          newestConstantStepIdx = i;
        }
        if (s.type === 'allocRegister' && s.registerId !== undefined) {
          registerCount = s.registerId + 1;
          newestRegisterId = s.registerId;
          newestRegisterStepIdx = i;
        }
      }
    }

    // Only highlight "newest" if the current step is the exact step that produced it
    const highlightNewInstr = newestInstrStepIdx === state.stepIndex;
    const highlightPatch = newestPatchStepIdx === state.stepIndex;
    const highlightNewConst = newestConstantStepIdx === state.stepIndex;
    const highlightNewReg = newestRegisterStepIdx === state.stepIndex;

    const instructions = func.instructions;
    const constants = func.constants;

    // Section wrapper
    const sectionCls = 'cl-func-section' + (isActive ? ' cl-func-section-active' : '');
    html += '<div class="' + sectionCls + '">';

    // Header with metadata (reflects incremental state)
    const metaParts: string[] = [];
    if (registerCount > 0) metaParts.push(registerCount + ' reg');
    if (constantsSoFar.length > 0) metaParts.push(constantsSoFar.length + ' const');
    if (func.upvalueDescs.length > 0 && isComplete && func.exitStepIndex <= state.stepIndex) {
      metaParts.push(func.upvalueDescs.length + ' uv');
    }

    html += '<div class="cl-func-section-header">';
    html += '<span>' + escapeHtml(func.name) + '</span>';
    if (metaParts.length > 0) {
      html += '<span class="cl-func-section-meta">' + escapeHtml(metaParts.join(', ')) + '</span>';
    }
    html += '</div>';

    // Instructions listing
    html += '<div class="bytecode-listing">';

    const closureOpcodes = new Set(['Closure', 'GetUpvalue', 'SetUpvalue', 'CloseUpvalue']);

    // Build patch targets set — use same replay range as instructions
    const allPatchTargets = new Set<number>();
    {
      let sd = 0;
      for (let si = 0; si <= replayEnd; si++) {
        const s = data.steps[si];
        if (s.type === 'enterFunction') sd++;
        else if (s.type === 'exitFunction') sd--;
        if (sd === funcDepthLevel && s.type === 'patchJump' && s.instrIndex !== undefined) {
          allPatchTargets.add(s.instrIndex);
        }
      }
    }

    const patchedJumps = new Map<number, number>();
    for (const p of allPatches) {
      patchedJumps.set(p.from, p.to);
    }

    const jumpTargets = new Set<number>();
    for (const [, target] of patchedJumps) {
      jumpTargets.add(target);
    }
    for (let i = 0; i < instrCount && i < instructions.length; i++) {
      const instr = instructions[i];
      if (instr.opcode === 'Jump' || instr.opcode === 'JumpIfTrue' || instr.opcode === 'JumpIfFalse') {
        if (!allPatchTargets.has(i)) {
          jumpTargets.add(i + 1 + instr.sbx);
        }
      }
    }

    for (let i = 0; i < instrCount && i < instructions.length; i++) {
      const instr = instructions[i];
      const addr = String(i).padStart(4, '0');
      const opName = instr.opcode;
      const isJump = opName === 'Jump' || opName === 'JumpIfTrue' || opName === 'JumpIfFalse';
      const isPlaceholder = isJump && allPatchTargets.has(i);
      const isPatched = patchedJumps.has(i);
      const isClosureOp = closureOpcodes.has(opName);

      const operands = formatOperands(instr);

      let cls = 'bytecode-line';
      if (highlightNewInstr && i === newestInstrIndex) {
        cls += ' bytecode-line-new';
      }
      if (highlightPatch && i === newestPatchIndex) {
        cls += ' bytecode-line-patched';
      }
      if (isClosureOp && cls === 'bytecode-line') {
        cls += ' cl-opcode-closure';
      }

      let targetLabel = '';
      if (jumpTargets.has(i)) {
        targetLabel = '<span class="cf-jump-label">\u25B8</span>';
      }

      let comment = '';
      if ((opName === 'LoadConst' || opName === 'GetGlobal' || opName === 'SetGlobal') &&
          instr.bx < constants.length) {
        comment = ' ; ' + constants[instr.bx];
      }
      if (opName === 'Closure' && instr.bx < constants.length) {
        comment = ' ; ' + constants[instr.bx];
      }
      if (opName === 'GetUpvalue' || opName === 'SetUpvalue') {
        comment = ' ; UV[' + instr.b + ']';
      }

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

      if (highlightPatch && i === newestPatchIndex && patchTarget >= 0) {
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

    html += '</div>'; // bytecode-listing

    // Show detailed constants and registers for the active (currently compiling) function
    if (isActive) {
      if (constantsSoFar.length > 0) {
        html += '<div class="bytecode-section">';
        html += '<div class="bytecode-section-header">Constants</div>';
        for (const idx of constantsSoFar) {
          const isNew = highlightNewConst && newestConstantIndex === idx;
          const cls = isNew ? 'constant-entry constant-new' : 'constant-entry';
          const val = idx < constants.length ? constants[idx] : '?';
          html += '<div class="' + cls + '">K' + idx + ' = ' + escapeHtml(val) + '</div>';
        }
        html += '</div>';
      }

      if (registerCount > 0) {
        // Simulate register contents for active function
        const registerDescs: string[] = new Array(registerCount).fill('');
        for (let idx = 0; idx < instrCount && idx < instructions.length; idx++) {
          const instr = instructions[idx];
          const op = instr.opcode;
          const a = instr.a;
          if (a >= registerCount) continue;

          switch (op) {
            case 'LoadConst': {
              const val = instr.bx < constants.length ? constants[instr.bx] : 'K' + instr.bx;
              registerDescs[a] = val;
              break;
            }
            case 'LoadTrue': registerDescs[a] = 'true'; break;
            case 'LoadFalse': registerDescs[a] = 'false'; break;
            case 'LoadNull': registerDescs[a] = 'null'; break;
            case 'LoadUndef': registerDescs[a] = 'undefined'; break;
            case 'GetGlobal': {
              const name = instr.bx < constants.length ? constants[instr.bx] : 'K' + instr.bx;
              registerDescs[a] = name;
              break;
            }
            case 'SetGlobal': break;
            case 'Move': registerDescs[a] = registerDescs[instr.b] || 'R' + instr.b; break;
            case 'GetUpvalue': registerDescs[a] = 'UV[' + instr.b + ']'; break;
            case 'SetUpvalue': break;
            case 'Closure': registerDescs[a] = '<closure F' + instr.bx + '>'; break;
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

        html += '<div class="bytecode-section">';
        html += '<div class="bytecode-section-header">Registers</div>';
        for (let r = 0; r < registerCount; r++) {
          const isNew = highlightNewReg && r === newestRegisterId;
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
    }

    // Upvalue Descriptors subsection — show once the function's exit step is reached
    if (isComplete && func.exitStepIndex >= 0 && func.exitStepIndex <= state.stepIndex && func.upvalueDescs.length > 0) {
      const uvNames = buildUpvalueNameMap(data.steps, func.enterStepIndex, func.exitStepIndex);

      html += '<div class="cl-uvdesc-section">';
      html += '<div class="cl-uvdesc-label">Upvalue Descriptors</div>';
      html += '<table class="cl-upvalue-table">';
      html += '<tr><th>UV#</th><th>is_local</th><th>index</th><th>Variable</th></tr>';
      for (const desc of func.upvalueDescs) {
        html += '<tr>';
        html += '<td>UV' + desc.index + '</td>';
        html += '<td>';
        if (desc.isLocal) {
          html += '<span class="cl-is-local-badge cl-is-local-true">true</span>';
        } else {
          html += '<span class="cl-is-local-badge cl-is-local-false">false</span>';
        }
        html += '</td>';
        html += '<td>' + desc.index + '</td>';
        html += '<td>' + escapeHtml(uvNames.get(desc.index) || '') + '</td>';
        html += '</tr>';
      }
      html += '</table>';
      html += '</div>';
    }

    html += '</div>'; // cl-func-section
  }

  if (visibleFuncs.length === 0) {
    html = '<div class="cf-empty">No functions compiled yet</div>';
  }

  state.bytecodeEl.innerHTML = html;

  if (!initializing) {
    const newLine = state.bytecodeEl.querySelector('.bytecode-line-new, .bytecode-line-patched');
    if (newLine) {
      newLine.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

// --- Feature 6: Interleaved Bytecode View ---

function renderInterleavedBytecode(state: ExampleState): void {
  if (!state.traceData) return;
  const data = state.traceData;
  const allFuncs = collectAllFunctions(data);
  const closureOpcodes = new Set(['Closure', 'GetUpvalue', 'SetUpvalue', 'CloseUpvalue']);

  // Build a map from function index to allFuncs entry for each depth level
  // The script-level children are data.functions[0], [1], etc.
  // We need to find child functions by bx (function index) within the parent's children.

  /**
   * Count how many instructions have been emitted for a given function up to the current step.
   */
  function countEmitted(func: FlatFunctionInfo): number {
    const replayEnd = (func.exitStepIndex >= 0 && func.exitStepIndex <= state.stepIndex)
      ? Math.min(func.exitStepIndex, state.stepIndex)
      : state.stepIndex;
    let count = 0;
    let scopeDepth = 0;
    for (let i = 0; i <= replayEnd; i++) {
      const s = data.steps[i];
      if (s.type === 'enterFunction') scopeDepth++;
      else if (s.type === 'exitFunction') scopeDepth--;
      if (scopeDepth === func.depth && s.type === 'emitInstruction' && s.instrIndex !== undefined) {
        count = s.instrIndex + 1;
      }
    }
    return count;
  }

  /**
   * Find the newest emitted instruction step index for a function.
   */
  function findNewestInstrStep(func: FlatFunctionInfo): { instrIndex: number; stepIdx: number } {
    const replayEnd = (func.exitStepIndex >= 0 && func.exitStepIndex <= state.stepIndex)
      ? Math.min(func.exitStepIndex, state.stepIndex)
      : state.stepIndex;
    let newestInstrIndex = -1;
    let newestStepIdx = -1;
    let scopeDepth = 0;
    for (let i = 0; i <= replayEnd; i++) {
      const s = data.steps[i];
      if (s.type === 'enterFunction') scopeDepth++;
      else if (s.type === 'exitFunction') scopeDepth--;
      if (scopeDepth === func.depth && s.type === 'emitInstruction' && s.instrIndex !== undefined) {
        newestInstrIndex = s.instrIndex;
        newestStepIdx = i;
      }
    }
    return { instrIndex: newestInstrIndex, stepIdx: newestStepIdx };
  }

  /**
   * Recursively render a function's instructions, inlining child functions after Closure opcodes.
   */
  function renderFuncInterleaved(func: FlatFunctionInfo, depth: number): string {
    const emitted = countEmitted(func);
    const { instrIndex: newestIdx, stepIdx: newestStepIdx } = findNewestInstrStep(func);
    const highlightNew = newestStepIdx === state.stepIndex;

    // Find child functions of this function
    const children = allFuncs.filter(f =>
      f.depth === func.depth + 1 &&
      f.enterStepIndex > func.enterStepIndex &&
      (func.exitStepIndex < 0 || f.enterStepIndex < func.exitStepIndex)
    );

    let html = '';
    let childCursor = 0;

    for (let i = 0; i < emitted && i < func.instructions.length; i++) {
      const instr = func.instructions[i];
      const addr = String(i).padStart(4, '0');
      const isClosureOp = closureOpcodes.has(instr.opcode);

      let cls = 'bytecode-line';
      if (highlightNew && i === newestIdx) cls += ' bytecode-line-new';
      else if (isClosureOp) cls += ' cl-opcode-closure';

      const operands = formatOperands(instr);
      let comment = '';
      if ((instr.opcode === 'LoadConst' || instr.opcode === 'GetGlobal' || instr.opcode === 'SetGlobal') &&
          instr.bx < func.constants.length) {
        comment = ' ; ' + func.constants[instr.bx];
      }
      if (instr.opcode === 'GetUpvalue' || instr.opcode === 'SetUpvalue') {
        comment = ' ; UV[' + instr.b + ']';
      }
      if (instr.opcode === 'Jump' || instr.opcode === 'JumpIfTrue' || instr.opcode === 'JumpIfFalse') {
        const target = i + 1 + instr.sbx;
        comment = ' -> [' + String(target).padStart(4, '0') + ']';
      }

      html += '<div class="' + cls + '">' +
        '<span class="bytecode-addr">' + addr + '</span>  ' +
        '<span class="bytecode-op">' + escapeHtml(instr.opcode) + '</span> ' +
        '<span class="bytecode-operands">' + escapeHtml(operands) + '</span>' +
        (comment ? '<span class="bytecode-comment">' + escapeHtml(comment) + '</span>' : '') +
        '</div>';

      // After a Closure instruction, inline the child function
      if (instr.opcode === 'Closure' && childCursor < children.length) {
        const child = children[childCursor];
        childCursor++;

        // Only show if the child has been entered
        if (child.enterStepIndex >= 0 && child.enterStepIndex <= state.stepIndex) {
          const metaParts: string[] = [];
          metaParts.push(child.registerCount + ' reg');
          metaParts.push(child.constants.length + ' const');
          if (child.upvalueDescs.length > 0) metaParts.push(child.upvalueDescs.length + ' uv');

          html += '<div class="cl-interleaved-bracket">';
          html += '<div class="cl-interleaved-header">\u250C\u2500\u2500 ' +
            escapeHtml(child.name) + ' (' + metaParts.join(', ') + ')</div>';
          html += renderFuncInterleaved(child, depth + 1);
          html += '<div class="cl-interleaved-footer">\u2514\u2500\u2500</div>';
          html += '</div>';
        }
      }
    }

    // Show any children that have been entered but haven't been attached to a
    // Closure instruction yet (child is compiled before the parent emits Closure).
    while (childCursor < children.length) {
      const child = children[childCursor];
      childCursor++;
      if (child.enterStepIndex >= 0 && child.enterStepIndex <= state.stepIndex) {
        const metaParts: string[] = [];
        metaParts.push(child.registerCount + ' reg');
        metaParts.push(child.constants.length + ' const');
        if (child.upvalueDescs.length > 0) metaParts.push(child.upvalueDescs.length + ' uv');

        const isChildActive = child.exitStepIndex < 0 || child.exitStepIndex > state.stepIndex;
        html += '<div class="cl-interleaved-bracket' + (isChildActive ? ' cl-interleaved-compiling' : '') + '">';
        html += '<div class="cl-interleaved-header">\u250C\u2500\u2500 ' +
          escapeHtml(child.name) +
          ' (' + metaParts.join(', ') + ')' +
          (isChildActive ? ' \u2190 compiling' : '') +
          '</div>';
        html += renderFuncInterleaved(child, depth + 1);
        html += '<div class="cl-interleaved-footer">\u2514\u2500\u2500</div>';
        html += '</div>';
      }
    }

    if (emitted === 0 && childCursor === 0) {
      html += '<div class="cf-empty">No instructions emitted yet</div>';
    }

    return html;
  }

  // Find the script-level function
  const scriptFunc = allFuncs[0];
  const currentFuncDepth = getCurrentFunctionDepth(data.steps, state.stepIndex);
  const isScriptActive = currentFuncDepth === 0;
  let html = '<div class="cl-func-section' + (isScriptActive ? ' cl-func-section-active' : '') + '">';
  html += '<div class="cl-func-section-header"><span>' + escapeHtml(scriptFunc.name) + '</span></div>';
  html += '<div class="bytecode-listing">';
  html += renderFuncInterleaved(scriptFunc, 0);
  html += '</div></div>';

  state.bytecodeEl.innerHTML = html;

  if (!initializing) {
    const newLine = state.bytecodeEl.querySelector('.bytecode-line-new');
    if (newLine) {
      newLine.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

// --- Feature 2: AST View ---

interface ParsedAstLine {
  depth: number;
  line: string;
  nodeType: string;
  isLabel: boolean;
}

function parseAstText(astString: string): ParsedAstLine[] {
  if (!astString) return [];
  const lines = astString.split('\n');
  const result: ParsedAstLine[] = [];
  for (const line of lines) {
    if (line.trim() === '') continue;
    const stripped = line.replace(/^ */, '');
    const leadingSpaces = line.length - stripped.length;
    const depth = Math.floor(leadingSpaces / 2);
    const trimmed = stripped.trim();

    // Labels end with ':' and may have counts like 'args: (2)'
    const isLabel = /^[a-z]+:/.test(trimmed);

    // Extract node type: text before '(' or end of line
    let nodeType = '';
    if (!isLabel) {
      const parenIdx = trimmed.indexOf('(');
      nodeType = parenIdx >= 0 ? trimmed.substring(0, parenIdx) : trimmed;
    }

    result.push({ depth, line: trimmed, nodeType, isLabel });
  }
  return result;
}

function renderAstView(state: ExampleState): void {
  if (!state.traceData || !state.traceData.ast) {
    state.astOverlayEl.innerHTML = '<div class="cf-empty">No AST data</div>';
    return;
  }

  const parsed = parseAstText(state.traceData.ast);
  const steps = state.traceData.steps;

  // Collect enterNode events in order (DFS order matches AST text order)
  const enterEvents: { nodeType: string; desc: string; stepIdx: number; exited: boolean }[] = [];
  const nodeStack: number[] = []; // stack of indices into enterEvents

  for (let i = 0; i <= state.stepIndex; i++) {
    const s = steps[i];
    if (s.type === 'enterNode') {
      enterEvents.push({
        nodeType: s.nodeType,
        desc: s.desc || '',
        stepIdx: i,
        exited: false,
      });
      nodeStack.push(enterEvents.length - 1);
    } else if (s.type === 'exitNode') {
      const top = nodeStack.pop();
      if (top !== undefined) {
        enterEvents[top].exited = true;
      }
    }
  }

  // Match enterEvents to AST lines greedily in order.
  // Only the innermost (top-of-stack) node should be 'active'.
  // Other on-stack ancestors are 'visited' (they've been entered, still open).
  const lineStates: ('active' | 'visited' | 'dimmed' | 'label')[] = new Array(parsed.length).fill('dimmed');
  let eventCursor = 0;
  const activeNodeIdx = nodeStack.length > 0 ? nodeStack[nodeStack.length - 1] : -1;

  for (let li = 0; li < parsed.length; li++) {
    const pl = parsed[li];
    if (pl.isLabel) {
      lineStates[li] = 'label';
      continue;
    }
    if (eventCursor < enterEvents.length) {
      const ev = enterEvents[eventCursor];
      if (pl.nodeType === ev.nodeType || pl.line.startsWith(ev.nodeType)) {
        if (eventCursor === activeNodeIdx) {
          lineStates[li] = 'active';
        } else {
          // Entered (and either exited, or an ancestor still on stack) → visited
          lineStates[li] = 'visited';
        }
        eventCursor++;
      }
    }
  }

  // Find the last active line for scrolling
  let activeLineIdx = -1;

  let html = '';
  for (let i = 0; i < parsed.length; i++) {
    const pl = parsed[i];
    const s = lineStates[i];
    let cls = 'cl-ast-line';
    if (s === 'active') { cls += ' cl-ast-active'; activeLineIdx = i; }
    else if (s === 'visited') cls += ' cl-ast-visited';
    else if (s === 'label') cls += ' cl-ast-label';
    else cls += ' cl-ast-dimmed';

    html += '<div class="' + cls + '" style="padding-left:' + (pl.depth * 16) + 'px">' +
      escapeHtml(pl.line) + '</div>';
  }

  state.astOverlayEl.innerHTML = html;

  if (!initializing && activeLineIdx >= 0) {
    const activeEl = state.astOverlayEl.children[activeLineIdx];
    if (activeEl) {
      activeEl.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

// --- Feature 3: Resolution Chain ---

/**
 * Find the enclosing enterNode/exitNode scope boundaries for the current step.
 */
function findEnclosingNodeScope(
  steps: CompilerStep[],
  currentIndex: number,
): { enterIdx: number; exitIdx: number } | null {
  // Walk backward to find enclosing enterNode (Identifier or AssignmentExpr)
  let depth = 0;
  let enterIdx = -1;
  for (let i = currentIndex; i >= 0; i--) {
    const s = steps[i];
    if (s.type === 'exitNode') depth++;
    else if (s.type === 'enterNode') {
      if (depth > 0) {
        depth--;
      } else {
        if (s.nodeType === 'Identifier' || s.nodeType === 'AssignmentExpr') {
          enterIdx = i;
          break;
        }
        // Keep searching up
      }
    }
  }
  if (enterIdx < 0) return null;

  // Walk forward to find the matching exitNode
  depth = 0;
  for (let i = enterIdx; i < steps.length; i++) {
    const s = steps[i];
    if (s.type === 'enterNode') depth++;
    else if (s.type === 'exitNode') {
      depth--;
      if (depth === 0) return { enterIdx, exitIdx: i };
    }
  }
  return { enterIdx, exitIdx: steps.length - 1 };
}

function renderResolutionChain(state: ExampleState): void {
  if (!state.traceData) {
    state.resolutionChainEl.classList.add('hidden');
    return;
  }

  const steps = state.traceData.steps;
  const step = steps[state.stepIndex];

  // Only show for resolution-related step types
  const resolutionTypes = new Set([
    'resolveLocal', 'resolveLocalNotFound', 'markCaptured', 'addUpvalue',
    'upvalueDedup', 'resolveUpvalue', 'resolveGlobal',
  ]);
  // Also show for emitInstruction of Get/SetUpvalue, Get/SetGlobal
  const isEmitResolution = step.type === 'emitInstruction' &&
    (step.desc === 'GetUpvalue' || step.desc === 'SetUpvalue' ||
     step.desc === 'GetGlobal' || step.desc === 'SetGlobal');

  if (!resolutionTypes.has(step.type) && !isEmitResolution) {
    state.resolutionChainEl.classList.add('hidden');
    return;
  }

  // Find the enclosing Identifier/AssignmentExpr scope
  const scope = findEnclosingNodeScope(steps, state.stepIndex);
  if (!scope) {
    state.resolutionChainEl.classList.add('hidden');
    return;
  }

  // Collect resolution-related steps, scoped to the right sub-expression.
  // Within an AssignmentExpr like `n = n + 1`, there are separate resolution
  // sequences for LHS `n` and RHS `n`. We partition by Identifier boundaries
  // and only show the cluster containing the current step.

  interface ChainStep {
    type: string;
    desc: string;
    category: 'local' | 'upvalue' | 'global' | 'emit';
    hit: boolean;
    stepIdx: number;
  }

  function isResolutionEmit(s: CompilerStep): boolean {
    return s.type === 'emitInstruction' &&
      (s.desc === 'GetUpvalue' || s.desc === 'SetUpvalue' ||
       s.desc === 'GetGlobal' || s.desc === 'SetGlobal');
  }

  function toChainStep(s: CompilerStep, idx: number): ChainStep | null {
    if (s.type === 'resolveLocal') return { type: s.type, desc: s.desc, category: 'local', hit: true, stepIdx: idx };
    if (s.type === 'resolveLocalNotFound') return { type: s.type, desc: s.desc, category: 'local', hit: false, stepIdx: idx };
    if (s.type === 'markCaptured') return { type: s.type, desc: s.desc, category: 'upvalue', hit: true, stepIdx: idx };
    if (s.type === 'addUpvalue' || s.type === 'upvalueDedup') return { type: s.type, desc: s.desc, category: 'upvalue', hit: true, stepIdx: idx };
    if (s.type === 'resolveUpvalue') return { type: s.type, desc: s.desc, category: 'upvalue', hit: true, stepIdx: idx };
    if (s.type === 'resolveGlobal') return { type: s.type, desc: s.desc, category: 'global', hit: false, stepIdx: idx };
    if (isResolutionEmit(s)) return { type: s.type, desc: s.desc, category: 'emit', hit: true, stepIdx: idx };
    return null;
  }

  // For Identifier scopes, just collect everything directly
  // For AssignmentExpr, partition into clusters separated by Identifier enter/exit boundaries
  const enterStep = steps[scope.enterIdx];
  const cappedEnd = Math.min(scope.exitIdx, state.stepIndex);

  let chainSteps: ChainStep[] = [];
  let varName = '';

  if (enterStep.nodeType === 'Identifier') {
    varName = enterStep.desc || '';
    for (let i = scope.enterIdx; i <= cappedEnd; i++) {
      const cs = toChainStep(steps[i], i);
      if (cs) chainSteps.push(cs);
    }
  } else {
    // AssignmentExpr or similar compound node — partition into clusters.
    // Each cluster starts when we see a resolveLocal/resolveLocalNotFound
    // at depth 0 within the scope (not inside a nested child node).
    // We collect all clusters, then pick the one containing the current step.
    const allStepsInScope: { cs: ChainStep; identName: string }[] = [];
    let currentIdentName = '';
    let nodeDepth = 0;

    for (let i = scope.enterIdx + 1; i <= cappedEnd; i++) {
      const s = steps[i];
      if (s.type === 'enterNode') {
        nodeDepth++;
        if (nodeDepth === 1 && s.nodeType === 'Identifier' && s.desc) {
          currentIdentName = s.desc;
        }
      } else if (s.type === 'exitNode') {
        nodeDepth--;
      }
      // Track variable name from resolution steps too
      if (s.variableName && (s.type === 'resolveLocal' || s.type === 'resolveLocalNotFound' ||
          s.type === 'resolveUpvalue' || s.type === 'resolveGlobal')) {
        currentIdentName = s.variableName;
      }
      const cs = toChainStep(s, i);
      if (cs) {
        allStepsInScope.push({ cs, identName: currentIdentName });
      }
    }

    // Find which cluster the current step falls in.
    // A cluster is a contiguous run of steps sharing the same identName.
    let clusterStart = -1;
    let clusterEnd = -1;
    let clusterName = '';
    for (let i = 0; i < allStepsInScope.length; i++) {
      if (allStepsInScope[i].cs.stepIdx === state.stepIndex) {
        clusterName = allStepsInScope[i].identName;
        // Walk backward to find cluster start
        clusterStart = i;
        while (clusterStart > 0 && allStepsInScope[clusterStart - 1].identName === clusterName) {
          clusterStart--;
        }
        // Walk forward to find cluster end
        clusterEnd = i;
        while (clusterEnd < allStepsInScope.length - 1 && allStepsInScope[clusterEnd + 1].identName === clusterName) {
          clusterEnd++;
        }
        break;
      }
    }

    if (clusterStart >= 0) {
      varName = clusterName;
      for (let i = clusterStart; i <= clusterEnd; i++) {
        chainSteps.push(allStepsInScope[i].cs);
      }
    }
  }

  if (chainSteps.length === 0) {
    state.resolutionChainEl.classList.add('hidden');
    return;
  }

  let html = '<div class="cl-resolution-header">Resolving: <strong>' + escapeHtml(varName || '?') + '</strong></div>';

  for (let i = 0; i < chainSteps.length; i++) {
    const cs = chainSteps[i];
    const isLast = i === chainSteps.length - 1;
    const isCurrent = cs.stepIdx === state.stepIndex;
    const connector = isLast ? '\u2514\u2500' : '\u251C\u2500';

    let categoryLabel = '';
    let categoryCls = '';
    switch (cs.category) {
      case 'local': categoryLabel = 'local?'; categoryCls = cs.hit ? 'cl-resolution-hit' : 'cl-resolution-miss'; break;
      case 'upvalue': categoryLabel = 'upvalue'; categoryCls = 'cl-resolution-hit'; break;
      case 'global': categoryLabel = 'global'; categoryCls = 'cl-resolution-miss'; break;
      case 'emit': categoryLabel = 'emit'; categoryCls = 'cl-resolution-hit'; break;
    }

    const stepCls = 'cl-resolution-step' + (isCurrent ? ' cl-resolution-active' : '');

    html += '<div class="' + stepCls + '">';
    html += '<span class="cl-resolution-connector">' + connector + '</span> ';
    html += '<span class="cl-resolution-category ' + categoryCls + '">[' + categoryLabel + ']</span> ';
    html += '<span class="cl-resolution-desc">' + escapeHtml(cs.desc) + '</span>';
    html += '</div>';
  }

  state.resolutionChainEl.innerHTML = html;
  state.resolutionChainEl.classList.remove('hidden');
}

function renderFunctionExplorer(state: ExampleState): void {
  if (!state.traceData) {
    state.functionExplorerEl.innerHTML = '<div class="cf-empty">No trace data</div>';
    return;
  }
  const data = state.traceData;
  const allFuncs = collectAllFunctions(data);
  const currentFuncDepth = getCurrentFunctionDepth(data.steps, state.stepIndex);

  let html = '';

  for (const func of allFuncs) {
    // Same visibility logic as renderBytecode
    if (func.depth > 0) {
      if (func.enterStepIndex < 0 || func.enterStepIndex > state.stepIndex) continue;
    }

    const isExited = func.exitStepIndex >= 0 && func.exitStepIndex <= state.stepIndex;
    const isBeingCompiled = func.depth > 0 && !isExited;
    const isActive = (func.depth === 0 && currentFuncDepth === 0) || isBeingCompiled;

    // Replay trace to get live counts for this function
    const replayEnd = (isExited && func.exitStepIndex >= 0)
      ? Math.min(func.exitStepIndex, state.stepIndex)
      : state.stepIndex;
    const funcDepthLevel = func.depth;

    let registerCount = 0;
    const constantsSoFar: number[] = [];
    const upvaluesSoFar: { index: number; isLocal: boolean; name: string }[] = [];
    let instrCount = 0;

    {
      let scopeDepth = 0;
      for (let i = 0; i <= replayEnd; i++) {
        const s = data.steps[i];
        if (s.type === 'enterFunction') scopeDepth++;
        else if (s.type === 'exitFunction') scopeDepth--;

        if (scopeDepth !== funcDepthLevel) continue;

        if (s.type === 'allocRegister' && s.registerId !== undefined) {
          registerCount = s.registerId + 1;
        }
        if (s.type === 'addConstant' && s.constantIndex !== undefined) {
          if (!constantsSoFar.includes(s.constantIndex)) {
            constantsSoFar.push(s.constantIndex);
          }
        }
        if (s.type === 'addUpvalue' && s.upvalueIndex !== undefined) {
          // Find variable name from nearby resolveUpvalue
          let varName = '';
          for (let j = i + 1; j <= replayEnd; j++) {
            if (data.steps[j].type === 'resolveUpvalue' && data.steps[j].variableName) {
              varName = data.steps[j].variableName!;
              break;
            }
            if (data.steps[j].type === 'enterFunction' || data.steps[j].type === 'exitFunction') break;
          }
          if (!varName) {
            for (let j = i - 1; j >= 0; j--) {
              if (data.steps[j].type === 'resolveUpvalue' && data.steps[j].variableName) {
                varName = data.steps[j].variableName!;
                break;
              }
              if (data.steps[j].type === 'enterFunction' || data.steps[j].type === 'exitFunction') break;
            }
          }
          if (!upvaluesSoFar.some(u => u.index === s.upvalueIndex)) {
            upvaluesSoFar.push({
              index: s.upvalueIndex,
              isLocal: s.isLocalUpvalue || false,
              name: varName,
            });
          }
        }
        if (s.type === 'emitInstruction' && s.instrIndex !== undefined) {
          instrCount = s.instrIndex + 1;
        }
      }
    }

    const cardCls = 'cl-func-card' + (isActive ? ' cl-func-card-active' : '');
    html += '<div class="' + cardCls + '">';

    // Header
    html += '<div class="cl-func-card-header">';
    html += '<span class="cl-func-card-name">' + escapeHtml(func.name) + '</span>';
    const metaParts: string[] = [];

    // Find paramCount from enterFunction step
    let paramCount = 0;
    if (func.enterStepIndex >= 0) {
      const enterStep = data.steps[func.enterStepIndex];
      if (enterStep && enterStep.paramCount !== undefined) paramCount = enterStep.paramCount;
    }
    if (paramCount > 0 || func.depth > 0) metaParts.push(paramCount + ' params');
    if (registerCount > 0) metaParts.push(registerCount + ' reg');
    if (constantsSoFar.length > 0) metaParts.push(constantsSoFar.length + ' const');
    if (upvaluesSoFar.length > 0) metaParts.push(upvaluesSoFar.length + ' uv');
    if (instrCount > 0) metaParts.push(instrCount + ' instr');

    html += '<span class="cl-func-card-meta">' + escapeHtml(metaParts.join(' · ')) + '</span>';
    html += '</div>';

    // Constants (expandable) — only show what's been added so far
    if (constantsSoFar.length > 0) {
      html += '<details class="cl-func-card-detail">';
      html += '<summary>Constants (' + constantsSoFar.length + ')</summary>';
      html += '<div class="cl-func-card-detail-body">';
      for (const idx of constantsSoFar) {
        const val = idx < func.constants.length ? func.constants[idx] : '?';
        html += '<div class="cl-func-card-entry">K' + idx + ': ' + escapeHtml(val) + '</div>';
      }
      html += '</div></details>';
    }

    // Upvalue Descriptors (expandable) — show incrementally as they're added
    if (upvaluesSoFar.length > 0) {
      html += '<details class="cl-func-card-detail">';
      html += '<summary>Upvalue Descriptors (' + upvaluesSoFar.length + ')</summary>';
      html += '<div class="cl-func-card-detail-body">';
      for (const uv of upvaluesSoFar) {
        html += '<div class="cl-func-card-entry">UV' + uv.index +
          ': is_local=' + uv.isLocal +
          (uv.name ? ' (' + escapeHtml(uv.name) + ')' : '') + '</div>';
      }
      html += '</div></details>';
    }

    // Instructions (expandable) — show only what's been emitted so far
    if (instrCount > 0) {
      html += '<details class="cl-func-card-detail">';
      html += '<summary>Instructions (' + instrCount + ')</summary>';
      html += '<div class="cl-func-card-detail-body cl-func-card-instr-list">';
      for (let ii = 0; ii < instrCount && ii < func.instructions.length; ii++) {
        const instr = func.instructions[ii];
        const idx = String(ii).padStart(4, '0');
        html += '<div class="cl-func-card-entry cl-func-card-instr">' +
          '<span class="cl-func-card-instr-idx">' + idx + '</span> ' +
          '<span class="bytecode-op">' + escapeHtml(instr.opcode) + '</span> ' +
          '<span class="cl-func-card-instr-ops">' + escapeHtml(formatOperands(instr)) + '</span>' +
          '</div>';
      }
      html += '</div></details>';
    }

    // Child Functions (expandable) — show children that have been entered so far
    if (func.childFunctions.length > 0) {
      // Count how many children are visible (entered) at this step
      const visibleChildren: { name: string; index: number }[] = [];
      for (let ci = 0; ci < func.childFunctions.length; ci++) {
        // Find the corresponding FlatFunctionInfo for this child
        const childFlat = allFuncs.find(f => f.depth === func.depth + 1 && f.childIndex === ci);
        if (childFlat && (childFlat.enterStepIndex < 0 || childFlat.enterStepIndex <= state.stepIndex)) {
          visibleChildren.push({ name: func.childFunctions[ci].name, index: ci });
        }
      }
      if (visibleChildren.length > 0) {
        html += '<details class="cl-func-card-detail">';
        html += '<summary>Child Functions (' + visibleChildren.length + '/' + func.childFunctions.length + ')</summary>';
        html += '<div class="cl-func-card-detail-body">';
        for (const child of visibleChildren) {
          html += '<div class="cl-func-card-entry">F' + child.index + ': ' + escapeHtml(child.name) + '</div>';
        }
        html += '</div></details>';
      }
    }

    html += '</div>'; // cl-func-card
  }

  if (!html) html = '<div class="cf-empty">No functions compiled yet</div>';
  state.functionExplorerEl.innerHTML = html;
}

function renderClosureContext(state: ExampleState): void {
  if (!state.traceData) return;
  const data = state.traceData;
  const step = data.steps[state.stepIndex];

  let html = '';

  // Scope Stack
  const scopeStack = reconstructScopeStack(data.steps, state.stepIndex);

  html += '<div class="cl-context-section">';
  html += '<div class="cl-context-section-header">Scope Stack</div>';
  html += '<div class="cl-scope-stack">';
  for (let i = 0; i < scopeStack.length; i++) {
    if (i > 0) html += '<span class="cl-scope-sep"> \u203A </span>';
    const isCurrent = i === scopeStack.length - 1;
    const cls = 'cl-scope-item' + (isCurrent ? ' cl-scope-item-current' : '');
    html += '<span class="' + cls + '">' + escapeHtml(scopeStack[i]) + '</span>';
  }
  html += '</div>';

  // Show enter/exit function event
  if (step.type === 'enterFunction') {
    html += '<div class="cl-func-event">' +
      '<span class="cl-func-event-badge cl-func-enter-badge">entered</span>' +
      ' Now compiling ' + escapeHtml(step.functionName || '?') +
      '</div>';
  } else if (step.type === 'exitFunction') {
    html += '<div class="cl-func-event">' +
      '<span class="cl-func-event-badge cl-func-exit-badge">exited</span>' +
      ' Returned to parent scope' +
      '</div>';
  }
  html += '</div>';

  // EnclosingState Chain
  const enclosingStates = reconstructEnclosingStates(data.steps, state.stepIndex);
  if (enclosingStates.length > 0) {
    html += '<div class="cl-context-section">';
    html += '<div class="cl-context-section-header">EnclosingState Chain</div>';
    html += '<div class="cl-enclosing-chain">';
    for (let i = enclosingStates.length - 1; i >= 0; i--) {
      const es = enclosingStates[i];
      const isNewest = i === enclosingStates.length - 1;
      // Highlight the card when markCaptured fires — it targets the immediately enclosing state
      const isCapturing = isNewest && step.type === 'markCaptured';
      const cardCls = 'cl-enclosing-card' +
        (isNewest ? ' cl-enclosing-card-newest' : '') +
        (isCapturing ? ' cl-enclosing-card-capturing' : '');

      html += '<div class="' + cardCls + '">';
      html += '<div class="cl-enclosing-card-header">';
      html += '<span class="cl-enclosing-func-name">' + escapeHtml(es.functionName) + '</span>';
      html += '<span class="cl-enclosing-depth">scope_depth: ' + es.scopeDepth + '</span>';
      html += '</div>';

      // Saved locals — show all fields from Local struct: name, reg, depth, is_captured
      html += '<div class="cl-enclosing-locals-section">';
      html += '<span class="cl-enclosing-sub-label">locals</span>';
      if (es.locals.length > 0) {
        html += '<table class="cl-enclosing-locals-table">';
        html += '<tr><th>name</th><th>reg</th><th>depth</th><th>is_captured</th></tr>';
        for (const local of es.locals) {
          const justCaptured = local.isCaptured &&
            step.type === 'markCaptured' &&
            step.variableName === local.name &&
            isNewest;
          const rowCls = justCaptured ? ' class="cl-enclosing-local-just-captured"' : '';
          html += '<tr' + rowCls + '>';
          html += '<td class="cl-enclosing-td-name">' + escapeHtml(local.name) + '</td>';
          html += '<td>R' + local.register + '</td>';
          html += '<td>' + local.depth + '</td>';
          html += '<td>';
          if (local.isCaptured) {
            html += '<span class="cl-captured-badge cl-captured-yes">true</span>';
          } else {
            html += '<span class="cl-captured-badge cl-captured-no">false</span>';
          }
          html += '</td>';
          html += '</tr>';
        }
        html += '</table>';
      } else {
        html += '<span class="cl-enclosing-empty">none</span>';
      }
      html += '</div>';

      // Saved upvalues — show all fields from UpvalueInfo struct: index, is_local
      html += '<div class="cl-enclosing-locals-section">';
      html += '<span class="cl-enclosing-sub-label">upvalues</span>';
      if (es.upvalues.length > 0) {
        html += '<table class="cl-enclosing-locals-table">';
        html += '<tr><th>index</th><th>is_local</th><th>variable</th></tr>';
        for (const uv of es.upvalues) {
          html += '<tr>';
          html += '<td>' + uv.index + '</td>';
          html += '<td>';
          if (uv.isLocal) {
            html += '<span class="cl-is-local-badge cl-is-local-true">true</span>';
          } else {
            html += '<span class="cl-is-local-badge cl-is-local-false">false</span>';
          }
          html += '</td>';
          html += '<td class="cl-enclosing-td-name">' + escapeHtml(uv.name || '') + '</td>';
          html += '</tr>';
        }
        html += '</table>';
      } else {
        html += '<span class="cl-enclosing-empty">none</span>';
      }
      html += '</div>';

      // Enclosing pointer
      html += '<div class="cl-enclosing-pointer">';
      if (i > 0) {
        html += 'enclosing \u2192 ' + escapeHtml(enclosingStates[i - 1].functionName);
      } else {
        html += 'enclosing \u2192 <span class="cl-enclosing-null">null</span>';
      }
      html += '</div>';
      html += '</div>';

      // Arrow between cards
      if (i > 0) {
        html += '<div class="cl-enclosing-arrow">\u2191</div>';
      }
    }
    html += '</div>';
    html += '</div>';
  }

  // Locals Table (for the current function scope)
  const locals = reconstructLocalsTable(data.steps, state.stepIndex);
  html += '<div class="cl-context-section">';
  html += '<div class="cl-context-section-header">Locals (current function)</div>';
  if (locals.length === 0) {
    html += '<div class="cf-empty">no locals</div>';
  } else {
    html += '<table class="cl-locals-table">';
    html += '<tr><th>Name</th><th>Reg</th><th>Depth</th><th>Captured</th></tr>';
    for (const local of locals) {
      const rowCls = (local.isCaptured && step.type === 'markCaptured' && step.variableName === local.name)
        ? ' class="cl-local-captured"' : '';
      html += '<tr' + rowCls + '>';
      html += '<td>' + escapeHtml(local.name) + '</td>';
      html += '<td>R' + local.register + '</td>';
      html += '<td>' + local.depth + '</td>';
      html += '<td>';
      if (local.isCaptured) {
        html += '<span class="cl-captured-badge cl-captured-yes">true</span>';
      } else {
        html += '<span class="cl-captured-badge cl-captured-no">false</span>';
      }
      html += '</td>';
      html += '</tr>';
    }
    html += '</table>';
  }
  html += '</div>';

  // Upvalue Table (for the current function scope)
  const upvalues = reconstructUpvalueTable(data.steps, state.stepIndex);
  html += '<div class="cl-context-section">';
  html += '<div class="cl-context-section-header">Upvalues (current function)</div>';
  if (upvalues.length === 0) {
    html += '<div class="cf-empty">no upvalues</div>';
  } else {
    html += '<table class="cl-upvalue-table">';
    html += '<tr><th>UV#</th><th>Src Idx</th><th>is_local</th><th>Variable</th></tr>';
    for (const uv of upvalues) {
      const isNew = step.type === 'addUpvalue' && step.upvalueIndex === uv.index;
      const rowCls = isNew ? ' class="cl-upvalue-new"' : '';
      html += '<tr' + rowCls + '>';
      html += '<td>UV' + uv.index + '</td>';
      html += '<td>' + (uv.sourceIndex >= 0 ? uv.sourceIndex : '-') + '</td>';
      html += '<td>';
      if (uv.isLocal) {
        html += '<span class="cl-is-local-badge cl-is-local-true">local</span>';
      } else {
        html += '<span class="cl-is-local-badge cl-is-local-false">upvalue</span>';
      }
      html += '</td>';
      html += '<td>' + escapeHtml(uv.name || '?') + '</td>';
      html += '</tr>';
    }
    html += '</table>';
  }
  html += '</div>';

  state.closureContextEl.innerHTML = html;
}

function renderSourceHighlight(state: ExampleState): void {
  if (!state.traceData) {
    state.sourceOverlayEl.classList.add('hidden');
    return;
  }
  const data = state.traceData;

  const nodeStack: { nodeType: string; line?: number; col?: number }[] = [];
  for (let i = 0; i <= state.stepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'enterNode') {
      nodeStack.push({ nodeType: s.nodeType, line: s.line, col: s.col });
    } else if (s.type === 'exitNode') {
      nodeStack.pop();
    }
  }

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

  if (!initializing) {
    const activeEl = state.sourceOverlayEl.querySelector('.cf-source-line-active');
    if (activeEl) {
      activeEl.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

// --- Runtime rendering ---

function renderRuntimeState(state: ExampleState): void {
  if (!state.runtimeData || state.runtimeData.steps.length === 0) return;
  const data = state.runtimeData;
  const step = data.steps[state.runtimeStepIndex];

  state.stepCounterEl.textContent =
    (state.runtimeStepIndex + 1) + ' / ' + data.steps.length;

  renderRuntimeStepInfo(state, step);

  // Bytecode: respect view mode toggle
  if (state.bytecodeViewMode === 'interleaved') {
    renderRuntimeInterleavedBytecode(state);
  } else {
    renderRuntimeBytecode(state);
  }

  // Context panel: respect tab toggle
  if (state.contextTabMode === 'explorer') {
    renderRuntimeFunctionExplorer(state);
  } else {
    renderRuntimeClosureContext(state);
  }

  renderRuntimeOutput(state);

  // Node stack not used in runtime mode — show function name
  state.nodeStackEl.innerHTML =
    '<span class="stack-item stack-current">' + escapeHtml(step.functionName) +
    ' (depth ' + step.callDepth + ')</span>';

  // Source highlight: hide in runtime mode (including AST overlay)
  state.sourceOverlayEl.classList.add('hidden');
  state.astOverlayEl.classList.add('hidden');
  state.editorEl.classList.remove('hidden');

  // Resolution chain: hide in runtime mode
  state.resolutionChainEl.classList.add('hidden');

  // Annotation: context-sensitive for closure ops
  renderRuntimeAnnotation(state, step);

  // Button states
  state.resetBtn.disabled = state.runtimeStepIndex === 0;
  state.prevBtn.disabled = state.runtimeStepIndex === 0;
  state.nextBtn.disabled = state.runtimeStepIndex >= data.steps.length - 1;
}

function renderRuntimeStepInfo(state: ExampleState, step: VMStep): void {
  let badgeClass = 'step-badge';
  let label: string = step.type;
  switch (step.type) {
    case 'execute':        badgeClass += ' step-badge-emitInstruction'; label = step.opcode; break;
    case 'call':           badgeClass += ' step-badge-enterFunction'; label = 'call'; break;
    case 'return':         badgeClass += ' step-badge-exitFunction'; label = 'return'; break;
    case 'captureUpvalue': badgeClass += ' step-badge-addUpvalue'; label = 'capture uv'; break;
    case 'closeUpvalue':   badgeClass += ' step-badge-markCaptured'; label = 'close uv'; break;
    case 'readUpvalue':    badgeClass += ' step-badge-resolveUpvalue'; label = 'read uv'; break;
    case 'writeUpvalue':   badgeClass += ' step-badge-resolveUpvalue'; label = 'write uv'; break;
  }

  state.stepInfoEl.innerHTML =
    '<span class="' + badgeClass + '">' + escapeHtml(label) + '</span> ' +
    '<span class="step-desc">' + escapeHtml(step.desc) + '</span>';
}

function renderRuntimeAnnotation(state: ExampleState, step: VMStep): void {
  let annotation = '';
  let pathType: 'local' | 'upvalue' | 'global' | null = null;

  switch (step.type) {
    case 'captureUpvalue':
      annotation = 'Capturing upvalue UV[' + (step.upvalueIndex ?? '?') + ']. ' +
        'Value: ' + (step.upvalueValue ?? '?') + '. ' +
        (step.upvalueIsOpen ? 'Open — still points to stack register.' :
          'Closed — value lives on heap.');
      pathType = 'upvalue';
      break;
    case 'closeUpvalue':
      annotation = step.desc + '. Upvalues transition from open (pointing to stack) ' +
        'to closed (value copied to heap). Future reads/writes go through the heap copy.';
      pathType = 'upvalue';
      break;
    case 'readUpvalue':
      annotation = 'Reading upvalue UV[' + (step.upvalueIndex ?? '?') + '] = ' +
        (step.upvalueValue ?? '?') + '. ' +
        (step.upvalueIsOpen ? 'Still open — reading from stack register.' :
          'Closed — reading from heap copy.');
      pathType = 'upvalue';
      break;
    case 'writeUpvalue':
      annotation = 'Writing upvalue UV[' + (step.upvalueIndex ?? '?') + '] = ' +
        (step.upvalueValue ?? '?') + '. ' +
        (step.upvalueIsOpen ? 'Still open — writing to stack register.' :
          'Closed — writing to heap copy.');
      pathType = 'upvalue';
      break;
    case 'call':
      annotation = step.desc + '. A new call frame is pushed onto the stack with ' +
        'its own register window.';
      pathType = 'local';
      break;
    case 'return':
      annotation = step.desc + '. The call frame is popped. Open upvalues in this ' +
        'frame\'s registers are closed before returning.';
      pathType = 'local';
      break;
    case 'execute':
      if (step.opcode === 'Closure') {
        annotation = step.desc + '. ' +
          (step.upvalueCount !== undefined && step.upvalueCount > 0
            ? 'See captured upvalue details in the next step(s).'
            : 'No upvalues to capture.');
        pathType = 'upvalue';
      }
      break;
  }

  if (!annotation) {
    state.annotationEl.classList.add('hidden');
    state.annotationEl.className = 'cl-annotation hidden';
    return;
  }

  state.annotationEl.textContent = annotation;
  let cls = 'cl-annotation';
  if (pathType === 'local') cls += ' cl-annotation-local';
  else if (pathType === 'upvalue') cls += ' cl-annotation-upvalue';
  else if (pathType === 'global') cls += ' cl-annotation-global';
  state.annotationEl.className = cls;
}

/** Collect all functions into a flat list from VMTraceProgram (reuse ChildFunction tree). */
function collectRuntimeFunctions(program: VMTraceProgram): {
  name: string;
  instructions: CompilerInstruction[];
  constants: string[];
  registerCount: number;
  upvalueDescs: UpvalueDesc[];
  childFunctions: ChildFunction[];
}[] {
  const result: {
    name: string;
    instructions: CompilerInstruction[];
    constants: string[];
    registerCount: number;
    upvalueDescs: UpvalueDesc[];
    childFunctions: ChildFunction[];
  }[] = [];

  // Script level
  result.push({
    name: '<script>',
    instructions: program.instructions,
    constants: program.constants,
    registerCount: program.registerCount,
    upvalueDescs: [],
    childFunctions: program.functions,
  });

  function walk(children: ChildFunction[]): void {
    for (const child of children) {
      result.push({
        name: child.name,
        instructions: child.instructions,
        constants: child.constants,
        registerCount: child.registerCount,
        upvalueDescs: child.upvalueDescs,
        childFunctions: child.functions,
      });
      walk(child.functions);
    }
  }
  walk(program.functions);

  return result;
}

function renderRuntimeBytecode(state: ExampleState): void {
  if (!state.runtimeData) return;
  const data = state.runtimeData;
  const step = data.steps[state.runtimeStepIndex];
  const allFuncs = collectRuntimeFunctions(data.program);

  let html = '';

  for (const func of allFuncs) {
    const isActive = func.name === step.functionName;
    const sectionCls = 'cl-func-section' + (isActive ? ' cl-func-section-active' : '');
    html += '<div class="' + sectionCls + '">';

    // Header
    const metaParts: string[] = [];
    if (func.registerCount > 0) metaParts.push(func.registerCount + ' reg');
    if (func.constants.length > 0) metaParts.push(func.constants.length + ' const');
    if (func.upvalueDescs.length > 0) metaParts.push(func.upvalueDescs.length + ' uv');

    html += '<div class="cl-func-section-header">';
    html += '<span>' + escapeHtml(func.name) + '</span>';
    if (metaParts.length > 0) {
      html += '<span class="cl-func-section-meta">' + escapeHtml(metaParts.join(', ')) + '</span>';
    }
    html += '</div>';

    // Instructions
    html += '<div class="bytecode-listing">';
    const closureOpcodes = new Set(['Closure', 'GetUpvalue', 'SetUpvalue', 'CloseUpvalue']);

    for (let i = 0; i < func.instructions.length; i++) {
      const instr = func.instructions[i];
      const addr = String(i).padStart(4, '0');
      const isClosureOp = closureOpcodes.has(instr.opcode);

      // Highlight current IP
      const isCurrentIP = isActive && i === step.ip;

      let cls = 'bytecode-line';
      if (isCurrentIP) {
        cls += ' cl-ip-highlight';
      } else if (isClosureOp) {
        cls += ' cl-opcode-closure';
      }

      const operands = formatOperands(instr);
      let comment = '';
      if ((instr.opcode === 'LoadConst' || instr.opcode === 'GetGlobal' || instr.opcode === 'SetGlobal') &&
          instr.bx < func.constants.length) {
        comment = ' ; ' + func.constants[instr.bx];
      }
      if (instr.opcode === 'GetUpvalue' || instr.opcode === 'SetUpvalue') {
        comment = ' ; UV[' + instr.b + ']';
      }
      if (instr.opcode === 'Jump' || instr.opcode === 'JumpIfTrue' || instr.opcode === 'JumpIfFalse') {
        const target = i + 1 + instr.sbx;
        comment = ' -> [' + String(target).padStart(4, '0') + ']';
      }

      html += '<div class="' + cls + '">' +
        '<span class="bytecode-addr">' + addr + '</span>  ' +
        '<span class="bytecode-op">' + escapeHtml(instr.opcode) + '</span> ' +
        '<span class="bytecode-operands">' + escapeHtml(operands) + '</span>' +
        (comment ? '<span class="bytecode-comment">' + escapeHtml(comment) + '</span>' : '') +
        '</div>';
    }

    html += '</div>'; // bytecode-listing

    // Upvalue Descriptors
    if (func.upvalueDescs.length > 0) {
      html += '<div class="cl-uvdesc-section">';
      html += '<div class="cl-uvdesc-label">Upvalue Descriptors</div>';
      html += '<table class="cl-upvalue-table">';
      html += '<tr><th>UV#</th><th>is_local</th><th>index</th></tr>';
      for (let i = 0; i < func.upvalueDescs.length; i++) {
        const desc = func.upvalueDescs[i];
        html += '<tr>';
        html += '<td>UV' + i + '</td>';
        html += '<td>';
        if (desc.isLocal) {
          html += '<span class="cl-is-local-badge cl-is-local-true">true</span>';
        } else {
          html += '<span class="cl-is-local-badge cl-is-local-false">false</span>';
        }
        html += '</td>';
        html += '<td>' + desc.index + '</td>';
        html += '</tr>';
      }
      html += '</table>';
      html += '</div>';
    }

    html += '</div>'; // cl-func-section
  }

  state.bytecodeEl.innerHTML = html;

  // Scroll to current IP
  if (!initializing) {
    const ipLine = state.bytecodeEl.querySelector('.cl-ip-highlight');
    if (ipLine) {
      ipLine.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

function renderRuntimeInterleavedBytecode(state: ExampleState): void {
  if (!state.runtimeData) return;
  const data = state.runtimeData;
  const step = data.steps[state.runtimeStepIndex];
  const closureOpcodes = new Set(['Closure', 'GetUpvalue', 'SetUpvalue', 'CloseUpvalue']);

  // Build a tree-aware list from the flat runtime functions
  const allFuncs = collectRuntimeFunctions(data.program);

  function renderRuntimeFuncInterleaved(
    func: { name: string; instructions: CompilerInstruction[]; constants: string[]; registerCount: number; upvalueDescs: UpvalueDesc[] },
    children: ChildFunction[],
  ): string {
    const isActive = func.name === step.functionName;
    let html = '';
    let childCursor = 0;

    for (let i = 0; i < func.instructions.length; i++) {
      const instr = func.instructions[i];
      const addr = String(i).padStart(4, '0');
      const isClosureOp = closureOpcodes.has(instr.opcode);
      const isCurrentIP = isActive && i === step.ip;

      let cls = 'bytecode-line';
      if (isCurrentIP) cls += ' cl-ip-highlight';
      else if (isClosureOp) cls += ' cl-opcode-closure';

      const operands = formatOperands(instr);
      let comment = '';
      if ((instr.opcode === 'LoadConst' || instr.opcode === 'GetGlobal' || instr.opcode === 'SetGlobal') &&
          instr.bx < func.constants.length) {
        comment = ' ; ' + func.constants[instr.bx];
      }
      if (instr.opcode === 'GetUpvalue' || instr.opcode === 'SetUpvalue') {
        comment = ' ; UV[' + instr.b + ']';
      }
      if (instr.opcode === 'Jump' || instr.opcode === 'JumpIfTrue' || instr.opcode === 'JumpIfFalse') {
        const target = i + 1 + instr.sbx;
        comment = ' -> [' + String(target).padStart(4, '0') + ']';
      }

      html += '<div class="' + cls + '">' +
        '<span class="bytecode-addr">' + addr + '</span>  ' +
        '<span class="bytecode-op">' + escapeHtml(instr.opcode) + '</span> ' +
        '<span class="bytecode-operands">' + escapeHtml(operands) + '</span>' +
        (comment ? '<span class="bytecode-comment">' + escapeHtml(comment) + '</span>' : '') +
        '</div>';

      // After Closure instruction, inline the child function
      if (instr.opcode === 'Closure' && childCursor < children.length) {
        const child = children[childCursor];
        childCursor++;

        const metaParts: string[] = [];
        metaParts.push(child.registerCount + ' reg');
        metaParts.push(child.constants.length + ' const');
        if (child.upvalueDescs.length > 0) metaParts.push(child.upvalueDescs.length + ' uv');

        html += '<div class="cl-interleaved-bracket">';
        html += '<div class="cl-interleaved-header">\u250C\u2500\u2500 ' +
          escapeHtml(child.name) + ' (' + metaParts.join(', ') + ')</div>';
        html += renderRuntimeFuncInterleaved(
          { name: child.name, instructions: child.instructions, constants: child.constants, registerCount: child.registerCount, upvalueDescs: child.upvalueDescs },
          child.functions,
        );
        html += '<div class="cl-interleaved-footer">\u2514\u2500\u2500</div>';
        html += '</div>';
      }
    }

    return html;
  }

  // Start with script-level function
  const scriptFunc = allFuncs[0];
  const isScriptActive = scriptFunc.name === step.functionName;
  let html = '<div class="cl-func-section' + (isScriptActive ? ' cl-func-section-active' : '') + '">';
  html += '<div class="cl-func-section-header"><span>' + escapeHtml(scriptFunc.name) + '</span></div>';
  html += '<div class="bytecode-listing">';
  html += renderRuntimeFuncInterleaved(scriptFunc, data.program.functions);
  html += '</div></div>';

  state.bytecodeEl.innerHTML = html;

  if (!initializing) {
    const ipLine = state.bytecodeEl.querySelector('.cl-ip-highlight');
    if (ipLine) {
      ipLine.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
  }
}

function renderRuntimeFunctionExplorer(state: ExampleState): void {
  if (!state.runtimeData) {
    state.functionExplorerEl.innerHTML = '<div class="cf-empty">No runtime data</div>';
    return;
  }
  const data = state.runtimeData;
  const step = data.steps[state.runtimeStepIndex];
  const allFuncs = collectRuntimeFunctions(data.program);

  let html = '';

  for (const func of allFuncs) {
    const isActive = func.name === step.functionName;
    const cardCls = 'cl-func-card' + (isActive ? ' cl-func-card-active' : '');
    html += '<div class="' + cardCls + '">';

    html += '<div class="cl-func-card-header">';
    html += '<span class="cl-func-card-name">' + escapeHtml(func.name) + '</span>';
    const metaParts: string[] = [];
    if (func.registerCount > 0) metaParts.push(func.registerCount + ' reg');
    if (func.constants.length > 0) metaParts.push(func.constants.length + ' const');
    if (func.upvalueDescs.length > 0) metaParts.push(func.upvalueDescs.length + ' uv');
    metaParts.push(func.instructions.length + ' instr');
    html += '<span class="cl-func-card-meta">' + escapeHtml(metaParts.join(' · ')) + '</span>';
    html += '</div>';

    // Constants
    if (func.constants.length > 0) {
      html += '<details class="cl-func-card-detail">';
      html += '<summary>Constants (' + func.constants.length + ')</summary>';
      html += '<div class="cl-func-card-detail-body">';
      for (let i = 0; i < func.constants.length; i++) {
        html += '<div class="cl-func-card-entry">K' + i + ': ' + escapeHtml(func.constants[i]) + '</div>';
      }
      html += '</div></details>';
    }

    // Upvalue Descriptors
    if (func.upvalueDescs.length > 0) {
      html += '<details class="cl-func-card-detail">';
      html += '<summary>Upvalue Descriptors (' + func.upvalueDescs.length + ')</summary>';
      html += '<div class="cl-func-card-detail-body">';
      for (let i = 0; i < func.upvalueDescs.length; i++) {
        const desc = func.upvalueDescs[i];
        html += '<div class="cl-func-card-entry">UV' + i + ': index=' + desc.index +
          ', is_local=' + desc.isLocal + '</div>';
      }
      html += '</div></details>';
    }

    // Instructions
    if (func.instructions.length > 0) {
      html += '<details class="cl-func-card-detail">';
      html += '<summary>Instructions (' + func.instructions.length + ')</summary>';
      html += '<div class="cl-func-card-detail-body cl-func-card-instr-list">';
      for (let ii = 0; ii < func.instructions.length; ii++) {
        const instr = func.instructions[ii];
        const idx = String(ii).padStart(4, '0');
        const isCurrentIp = isActive && step.ip === ii;
        const cls = 'cl-func-card-entry cl-func-card-instr' + (isCurrentIp ? ' cl-func-card-instr-active' : '');
        html += '<div class="' + cls + '">' +
          '<span class="cl-func-card-instr-idx">' + idx + '</span> ' +
          '<span class="bytecode-op">' + escapeHtml(instr.opcode) + '</span> ' +
          '<span class="cl-func-card-instr-ops">' + escapeHtml(formatOperands(instr)) + '</span>' +
          '</div>';
      }
      html += '</div></details>';
    }

    // Child Functions
    if (func.childFunctions.length > 0) {
      html += '<details class="cl-func-card-detail">';
      html += '<summary>Child Functions (' + func.childFunctions.length + ')</summary>';
      html += '<div class="cl-func-card-detail-body">';
      for (let ci = 0; ci < func.childFunctions.length; ci++) {
        html += '<div class="cl-func-card-entry">F' + ci + ': ' + escapeHtml(func.childFunctions[ci].name) + '</div>';
      }
      html += '</div></details>';
    }

    html += '</div>';
  }

  state.functionExplorerEl.innerHTML = html;
}

function renderRuntimeClosureContext(state: ExampleState): void {
  if (!state.runtimeData) return;
  const data = state.runtimeData;
  const step = data.steps[state.runtimeStepIndex];

  let html = '';

  // Call Stack
  html += '<div class="cl-context-section">';
  html += '<div class="cl-context-section-header">Call Stack</div>';

  // Reconstruct call stack from steps up to current index
  const callStack: { functionName: string; baseRegister: number; }[] = [];
  for (let i = 0; i <= state.runtimeStepIndex; i++) {
    const s = data.steps[i];
    if (s.type === 'call') {
      // The call frame being pushed: find next step to get the callee info
      // The call step itself has the caller's function name and the description says the callee
      callStack.push({
        functionName: s.desc.split('(')[0].replace('Call ', ''),
        baseRegister: s.baseRegister,
      });
    } else if (s.type === 'return') {
      callStack.pop();
    }
  }

  // Always show current frame info based on the step itself
  html += '<div class="cl-call-stack">';
  // Show reconstructed stack plus current
  const frames = [{ functionName: '<script>', baseRegister: 0 }, ...callStack];
  for (let i = frames.length - 1; i >= 0; i--) {
    const frame = frames[i];
    const isTop = i === frames.length - 1;
    const cls = 'cl-call-stack-frame' + (isTop ? ' cl-call-stack-frame-active' : '');
    html += '<div class="' + cls + '">';
    html += '<span class="cl-call-stack-name">' + escapeHtml(frame.functionName) + '</span>';
    html += '<span class="cl-call-stack-base">base=' + frame.baseRegister + '</span>';
    html += '</div>';
  }
  html += '</div>';
  html += '</div>';

  // Register Writes (for current step)
  const regWrites = step.regWrites || [];
  html += '<div class="cl-context-section">';
  html += '<div class="cl-context-section-header">Register Writes</div>';
  if (regWrites.length === 0) {
    html += '<div class="cf-empty">no register writes</div>';
  } else {
    for (const rw of regWrites) {
      html += '<div class="cl-reg-write">R[' + rw.index + '] = ' +
        escapeHtml(rw.value) + '</div>';
    }
  }
  html += '</div>';

  // Upvalue State (for closure-related ops)
  if (step.type === 'readUpvalue' || step.type === 'writeUpvalue' ||
      step.type === 'captureUpvalue' || step.type === 'closeUpvalue') {
    html += '<div class="cl-context-section">';
    html += '<div class="cl-context-section-header">Upvalue State</div>';
    html += '<div class="cl-upvalue-state">';

    if (step.upvalueIndex !== undefined && step.upvalueIndex >= 0) {
      html += '<div class="cl-upvalue-state-entry">';
      html += '<span class="cl-upvalue-state-label">UV[' + step.upvalueIndex + ']</span>';
      if (step.upvalueVarName) {
        html += ' <span class="cl-upvalue-state-name">' + escapeHtml(step.upvalueVarName) + '</span>';
      }
      html += ' = <span class="cl-upvalue-state-value">' +
        escapeHtml(step.upvalueValue || '?') + '</span>';
      html += ' <span class="cl-upvalue-state-status ' +
        (step.upvalueIsOpen ? 'cl-upvalue-open' : 'cl-upvalue-closed') + '">' +
        (step.upvalueIsOpen ? 'open' : 'closed') + '</span>';
      html += '</div>';
    }

    // For Closure ops, show all captured upvalues by scanning nearby CaptureUpvalue steps
    if (step.type === 'captureUpvalue' && step.closureFuncIndex !== undefined) {
      html += '<div class="cl-upvalue-state-detail">';
      html += step.desc;
      html += '</div>';
    }

    html += '</div>';
    html += '</div>';
  }

  // Closure Info (for Closure execute step)
  if (step.opcode === 'Closure' && step.type === 'execute') {
    html += '<div class="cl-context-section">';
    html += '<div class="cl-context-section-header">Closure Info</div>';
    html += '<div class="cl-closure-info">';
    html += 'Function index: ' + (step.closureFuncIndex ?? '?');
    html += ', Upvalues: ' + (step.upvalueCount ?? 0);
    html += '</div>';
    html += '</div>';
  }

  state.closureContextEl.innerHTML = html;
}

function renderRuntimeOutput(state: ExampleState): void {
  if (!state.runtimeData) return;
  const output = state.runtimeData.output;
  const error = state.runtimeData.error;

  let html = '';
  if (output) {
    html += '<pre class="cl-output-text">' + escapeHtml(output) + '</pre>';
  }
  if (error) {
    html += '<div class="error">' + escapeHtml(error) + '</div>';
  }
  if (!output && !error) {
    html += '<div class="cf-empty">no output</div>';
  }
  state.outputEl.innerHTML = html;
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
  const astOverlayEl = el('pre', { className: 'cl-ast-overlay hidden' }) as HTMLElement;

  const compileBtn = el('button', { className: 'btn btn-primary cf-compile-btn' }, 'Compile & Step');

  // Mode toggle
  const compileModeBtn = el('button', { className: 'cl-mode-toggle-btn active' }, 'Compile') as HTMLButtonElement;
  const runtimeModeBtn = el('button', { className: 'cl-mode-toggle-btn', disabled: 'true' }, 'Runtime') as HTMLButtonElement;
  const modeToggleEl = el('div', { className: 'cl-mode-toggle' },
    compileModeBtn, runtimeModeBtn,
  );

  const stepCounterEl = el('span', { className: 'step-counter' });
  const resetBtn = el('button', { disabled: 'true' }, '\u23EE');
  const prevBtn = el('button', { disabled: 'true' }, '\u25C0') as HTMLButtonElement;
  const nextBtn = el('button', { disabled: 'true' }, '\u25B6') as HTMLButtonElement;
  const autoBtn = el('button', { className: 'step-auto-btn' }, '\u25B6');

  const stepControlsEl = el('div', { className: 'step-controls cf-step-controls' },
    modeToggleEl, resetBtn, prevBtn, nextBtn, autoBtn, stepCounterEl,
  );

  const stepInfoEl = el('div', { className: 'step-info cf-step-info' });
  const annotationEl = el('div', { className: 'cl-annotation hidden' }) as HTMLElement;

  const nodeStackEl = el('div', { className: 'cf-node-stack' });
  const nodeStackSection = el('div', { className: 'cf-section' },
    el('div', { className: 'cf-section-header' }, 'AST Node Stack'),
    nodeStackEl,
  );

  const bytecodeEl = el('div', { className: 'cf-bytecode-panel' });
  const bytecodePerFuncBtn = el('button', { className: 'cl-bytecode-view-btn active' }, 'Per-Function') as HTMLButtonElement;
  const bytecodeInterleavedBtn = el('button', { className: 'cl-bytecode-view-btn' }, 'Interleaved') as HTMLButtonElement;
  const bytecodeViewToggle = el('div', { className: 'cl-bytecode-view-toggle' },
    bytecodePerFuncBtn, bytecodeInterleavedBtn,
  );
  const bytecodeSection = el('div', { className: 'cf-section cf-section-bytecode' },
    el('div', { className: 'cf-section-header' },
      el('span', {}, 'Bytecode'),
      bytecodeViewToggle,
    ),
    bytecodeEl,
  );

  const closureContextEl = el('div', { className: 'cl-context-panel' });
  const functionExplorerEl = el('div', { className: 'cl-context-panel hidden' }) as HTMLElement;
  const contextTabBtnContext = el('button', { className: 'cl-context-tab-btn active' }, 'Context') as HTMLButtonElement;
  const contextTabBtnExplorer = el('button', { className: 'cl-context-tab-btn' }, 'Functions') as HTMLButtonElement;
  const contextTabBar = el('div', { className: 'cl-context-tab-bar' },
    contextTabBtnContext, contextTabBtnExplorer,
  );
  const closureContextSection = el('div', { className: 'cf-section cf-section-loop' },
    contextTabBar,
    closureContextEl,
    functionExplorerEl,
  );

  const outputEl = el('div', { className: 'cl-output-panel hidden' }) as HTMLElement;
  const outputSection = el('div', { className: 'cf-section cl-output-section hidden' },
    el('div', { className: 'cf-section-header' }, 'Output'),
    outputEl,
  );

  const descEl = el('div', { className: 'cf-description' }, example.description);

  const resolutionChainEl = el('div', { className: 'cl-resolution-chain hidden' }) as HTMLElement;

  const sourceTabBtnSource = el('button', { className: 'cl-source-tab-btn active' }, 'Source') as HTMLButtonElement;
  const sourceTabBtnAst = el('button', { className: 'cl-source-tab-btn' }, 'AST') as HTMLButtonElement;
  const sourceTabBar = el('div', { className: 'cl-source-tab-bar' },
    sourceTabBtnSource, sourceTabBtnAst,
  );

  const containerEl = el('div', { className: 'cf-card' },
    el('div', { className: 'cf-card-header' },
      el('h2', {}, example.title),
      compileBtn,
    ),
    descEl,
    el('div', { className: 'cf-card-body' },
      el('div', { className: 'cf-editor-col' },
        sourceTabBar,
        editorEl,
        sourceOverlayEl,
        astOverlayEl,
      ),
      el('div', { className: 'cf-output-col' },
        stepControlsEl,
        stepInfoEl,
        annotationEl,
        resolutionChainEl,
        nodeStackSection,
        el('div', { className: 'cf-output-split' },
          bytecodeSection,
          closureContextSection,
        ),
        outputSection,
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
    viewMode: 'compile',
    runtimeData: null,
    runtimeStepIndex: 0,
    contextTabMode: 'context',
    functionExplorerEl,
    leftTabMode: 'source',
    astOverlayEl,
    resolutionChainEl,
    bytecodeViewMode: 'perFunction',
    editorEl,
    sourceOverlayEl,
    stepInfoEl,
    annotationEl,
    stepCounterEl,
    bytecodeEl,
    closureContextEl,
    nodeStackEl,
    prevBtn: prevBtn as HTMLButtonElement,
    nextBtn: nextBtn as HTMLButtonElement,
    autoBtn: autoBtn as HTMLButtonElement,
    resetBtn: resetBtn as HTMLButtonElement,
    compileBtn: compileBtn as HTMLButtonElement,
    containerEl,
    modeToggleEl: modeToggleEl as HTMLElement,
    compileModeBtn: compileModeBtn as HTMLButtonElement,
    runtimeModeBtn: runtimeModeBtn as HTMLButtonElement,
    outputEl,
  };

  compileBtn.addEventListener('click', () => compileExample(state));
  nextBtn.addEventListener('click', () => stepNext(state));
  prevBtn.addEventListener('click', () => stepPrev(state));
  resetBtn.addEventListener('click', () => stepReset(state));
  autoBtn.addEventListener('click', () => toggleAutoPlay(state));
  compileModeBtn.addEventListener('click', () => switchMode(state, 'compile'));
  runtimeModeBtn.addEventListener('click', () => switchMode(state, 'runtime'));

  // Context panel tab toggle (Feature 1)
  contextTabBtnContext.addEventListener('click', () => {
    state.contextTabMode = 'context';
    contextTabBtnContext.classList.add('active');
    contextTabBtnExplorer.classList.remove('active');
    closureContextEl.classList.remove('hidden');
    functionExplorerEl.classList.add('hidden');
    renderView(state);
  });
  contextTabBtnExplorer.addEventListener('click', () => {
    state.contextTabMode = 'explorer';
    contextTabBtnExplorer.classList.add('active');
    contextTabBtnContext.classList.remove('active');
    functionExplorerEl.classList.remove('hidden');
    closureContextEl.classList.add('hidden');
    renderView(state);
  });

  // Source/AST tab toggle (Feature 2)
  sourceTabBtnSource.addEventListener('click', () => {
    state.leftTabMode = 'source';
    sourceTabBtnSource.classList.add('active');
    sourceTabBtnAst.classList.remove('active');
    renderView(state);
  });
  sourceTabBtnAst.addEventListener('click', () => {
    state.leftTabMode = 'ast';
    sourceTabBtnAst.classList.add('active');
    sourceTabBtnSource.classList.remove('active');
    renderView(state);
  });

  // Bytecode view toggle (Feature 6)
  bytecodePerFuncBtn.addEventListener('click', () => {
    state.bytecodeViewMode = 'perFunction';
    bytecodePerFuncBtn.classList.add('active');
    bytecodeInterleavedBtn.classList.remove('active');
    renderView(state);
  });
  bytecodeInterleavedBtn.addEventListener('click', () => {
    state.bytecodeViewMode = 'interleaved';
    bytecodeInterleavedBtn.classList.add('active');
    bytecodePerFuncBtn.classList.remove('active');
    renderView(state);
  });

  return state;
}

function buildPage(): void {
  const root = document.getElementById('app')!;

  const nav = el('nav', { className: 'app-nav' },
    el('a', { href: '../' }, 'Home'),
    el('a', { href: '../playground/' }, 'Playground'),
    el('a', { href: '../control-flow-compilation/' }, 'Control Flow'),
    el('a', { href: '../test262/' }, 'Test262 Runner'),
    el('a', { href: '../custom/' }, 'Custom Tests'),
  );

  const header = el('header', { className: 'app-header' },
    el('h1', {}, 'Yatsi'),
    el('p', { className: 'subtitle' }, 'Closures & Upvalues'),
    nav,
  );

  const intro = el('div', { className: 'cf-intro' },
    el('p', {},
      'Walk through how the compiler handles closures and upvalue capture. ' +
      'Each example shows the function scope stack, the EnclosingState chain (saved compiler state ' +
      'during nested function compilation), locals table with captured-variable tracking, ' +
      'and the upvalue descriptor table being built step by step.',
    ),
    el('div', { className: 'cf-legend' },
      el('span', { className: 'cf-legend-item' },
        el('span', { className: 'step-badge step-badge-enterFunction', style: 'font-size:10px;padding:0 4px' }, 'enter'),
        ' / ',
        el('span', { className: 'step-badge step-badge-exitFunction', style: 'font-size:10px;padding:0 4px' }, 'exit'),
        ' function scope',
      ),
      el('span', { className: 'cf-legend-item' },
        el('span', { className: 'step-badge step-badge-markCaptured', style: 'font-size:10px;padding:0 4px' }, 'captured'),
        ' local marked',
      ),
      el('span', { className: 'cf-legend-item' },
        el('span', { className: 'step-badge step-badge-resolveUpvalue', style: 'font-size:10px;padding:0 4px' }, 'resolve'),
        ' upvalue found',
      ),
      el('span', { className: 'cf-legend-item' },
        el('span', { className: 'step-badge step-badge-addUpvalue', style: 'font-size:10px;padding:0 4px' }, 'add uv'),
        ' descriptor added',
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

  // Keyboard shortcuts
  document.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.target instanceof HTMLTextAreaElement) return;

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

  for (const state of states) {
    compileExample(state);
  }
  initializing = false;
}

init();
