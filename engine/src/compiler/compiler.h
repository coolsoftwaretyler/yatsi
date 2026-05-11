#pragma once

#include "compiler/bytecode.h"
#include "parser/ast.h"
#include "runtime/gc.h"

namespace yatsi {

struct LoopContext {
  size_t continue_target; // where `continue` jumps to
  std::vector<size_t> break_jumps; // break indices we have to fill in with back-patching
  std::vector<size_t> continue_jumps; // continue jump indices to patch
  bool is_for_loop = false; // we have to indicate for loops for specific logic
};

class Compiler {
public:
  explicit Compiler(GarbageCollector& gc);

  BytecodeFunction compile(const Program& program);

private:
  GarbageCollector& gc_;
  BytecodeFunction* current_function_ = nullptr;
  std::vector<LoopContext> loop_stack_;

  // Register allocation (monotonic counter)
  uint8_t allocate_register();

  // Constant pool management
  uint16_t add_constant(Value val);
  uint16_t add_string_constant(const std::string& str);

  // Emit helpers
  void emit(Instruction instr);
  void emit_abc(OpCode op, uint8_t a, uint8_t b, uint8_t c);
  void emit_abx(OpCode op, uint8_t a, uint16_t bx);
  void emit_asbx(OpCode op, uint8_t a, int16_t sbx);

  // Jump/backpatching helpers
  size_t emit_jump(OpCode op, uint8_t a = 0); // emit a jump with offset 0, returns the index
  void patch_jump(size_t index); // patch to current position
  void patch_jump_to(size_t index, size_t target); // patches to specific target
  size_t current_offset(); // return the current code size

  // AST visitors
  void compile_stmt(const Stmt& stmt);
  uint8_t compile_expr(const Expr& expr); // returns register holding result
};

} // namespace yatsi
