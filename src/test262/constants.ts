/**
 * $262 shim and helpers prepended to test source.
 * This JS is evaluated by the WASM engine, not the browser.
 */
export const YATSI_262_SHIM = `
var __yatsi_print_buffer = "";

function print() {
  var args = [];
  for (var i = 0; i < arguments.length; i++) {
    args.push(String(arguments[i]));
  }
  __yatsi_print_buffer += args.join(" ") + "\\n";
}

function $DONOTEVALUATE() {
  throw new Test262Error("$DONOTEVALUATE was called");
}

var $262 = {
  createRealm: function() {
    throw new Test262Error("$262.createRealm is not supported");
  },
  detachArrayBuffer: function() {
    throw new Test262Error("$262.detachArrayBuffer is not supported");
  },
  evalScript: function() {
    throw new Test262Error("$262.evalScript is not supported");
  },
  gc: function() {
    throw new Test262Error("$262.gc is not supported");
  },
  global: this,
  agent: {
    start: function() { throw new Test262Error("$262.agent.start is not supported"); },
    broadcast: function() { throw new Test262Error("$262.agent.broadcast is not supported"); },
    getReport: function() { throw new Test262Error("$262.agent.getReport is not supported"); },
    sleep: function() { throw new Test262Error("$262.agent.sleep is not supported"); },
    monotonicNow: function() { throw new Test262Error("$262.agent.monotonicNow is not supported"); }
  }
};
`;

export const REQUIRED_HARNESS_FILES = ['assert.js', 'sta.js'];
export const ASYNC_HARNESS_FILE = 'doneprintHandle.js';
