#pragma once

#include "compiler/bytecode.h"
#include "parser/ast.h"
#include "runtime/gc.h"

namespace yatsi {

class Compiler {
public:
  explicit Compiler(GarbageCollector& gc);

  BytecodeFunction compile(const Program& program);

private:
  GarbageCollector& gc_;
  BytecodeFunction* current_function_ = nullptr;

  // Register allocation (monotonic counter)
  uint8_t allocate_register();

  // Constant pool management
  uint16_t add_constant(Value val);
  uint16_t add_string_constant(const std::string& str);

  // Emit helpers
  void emit(Instruction instr);
  void emit_abc(OpCode op, uint8_t a, uint8_t b, uint8_t c);
  void emit_abx(OpCode op, uint8_t a, uint16_t bx);

  // AST visitors
  void compile_stmt(const Stmt& stmt);
  uint8_t compile_expr(const Expr& expr); // returns register holding result
};

} // namespace yatsi
