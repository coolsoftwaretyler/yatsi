#pragma once

#include "common/value.h"
#include "compiler/bytecode.h"
#include "runtime/gc.h"
#include "vm/call_frame.h"

#include <array>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace yatsi {

struct Upvalue;

enum class InterpretResult {
  Ok,
  RuntimeError,
};

class VM {
public:
  explicit VM(GarbageCollector &gc);
  VM(GarbageCollector &gc, std::ostream &out);

  InterpretResult execute(BytecodeFunction &func);

  // Read a register value (for testing/debugging)
  const Value &get_register(uint8_t index) const;

  // Garbage collection: mark roots then sweep
  void collect_garbage();

private:
  GarbageCollector &gc_;
  std::ostream &out_;

  // Flat register file — each CallFrame gets a window into this
  static constexpr size_t kMaxRegisters = 256 * 64;
  // Protect against stack overflow, limit call depth to 256
  static constexpr size_t kMaxCallDepth = 256;
  std::array<Value, kMaxRegisters> registers_;

  // Call stack
  std::vector<CallFrame> call_stack_;

  // Global variables (name -> value)
  std::unordered_map<std::string, Value> globals_;

  // Open upvalue linked list
  Upvalue *open_upvalues_ = nullptr;

  // Access a register relative to the current frame's base
  Value &reg(uint8_t index);

  // Current call frame
  CallFrame &current_frame();

  // Upvalue helpers
  Upvalue *capture_upvalue(uint16_t abs_reg);
  void close_upvalues(uint16_t from_reg);

  // Mark all GC roots reachable from the VM
  void mark_roots();
};

} // namespace yatsi
