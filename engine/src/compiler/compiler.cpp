#include "compiler/compiler.h"

#include "runtime/js_string.h"

#include <iostream>

namespace yatsi {

// --- Tracing infrastructure ---

void Compiler::enable_tracing(std::vector<CompilerStep>& trace) {
  trace_ = &trace;
}

void Compiler::trace_step(CompilerStep step) {
  if (trace_) {
    step.depth = trace_depth_;
    trace_->push_back(std::move(step));
  }
}

// RAII helper — emits EnterNode on construction, ExitNode on destruction
struct TraceNode {
  std::vector<CompilerStep>* trace_;
  size_t* depth_;
  std::string node_type_;

  TraceNode(std::vector<CompilerStep>* t, size_t* depth,
            const std::string& node_type, const std::string& detail)
      : trace_(t), depth_(depth), node_type_(node_type) {
    if (trace_) {
      CompilerStep step;
      step.type = CompilerStep::Type::EnterNode;
      step.depth = *depth_;
      step.node_type = node_type;
      step.description = detail;
      trace_->push_back(std::move(step));
      (*depth_)++;
    }
  }

  ~TraceNode() {
    if (trace_) {
      (*depth_)--;
      CompilerStep step;
      step.type = CompilerStep::Type::ExitNode;
      step.depth = *depth_;
      step.node_type = node_type_;
      step.description = "done";
      trace_->push_back(std::move(step));
    }
  }
};

static std::string expr_node_desc(const Expr& e) {
  return std::visit([](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, NumberLiteral>) {
      if (v.value == static_cast<int64_t>(v.value))
        return "value=" + std::to_string(static_cast<int64_t>(v.value));
      return "value=" + std::to_string(v.value);
    }
    else if constexpr (std::is_same_v<T, StringLiteral>) return "\"" + v.value + "\"";
    else if constexpr (std::is_same_v<T, BooleanLiteral>) return v.value ? "true" : "false";
    else if constexpr (std::is_same_v<T, NullLiteral>) return "null";
    else if constexpr (std::is_same_v<T, UndefinedLiteral>) return "undefined";
    else if constexpr (std::is_same_v<T, Identifier>) return v.name;
    else if constexpr (std::is_same_v<T, BinaryExpr>) return std::string(token_kind_to_string(v.op));
    else if constexpr (std::is_same_v<T, UnaryExpr>) return std::string(token_kind_to_string(v.op));
    else if constexpr (std::is_same_v<T, AssignmentExpr>) return std::string(token_kind_to_string(v.op));
    else if constexpr (std::is_same_v<T, CallExpr>) return "";
    else if constexpr (std::is_same_v<T, MemberExpr>) return v.is_computed ? "[]" : "." + v.property;
    else return "";
  }, static_cast<const Expr::variant&>(e));
}

Compiler::Compiler(GarbageCollector &gc) : gc_(gc) {}

BytecodeFunction Compiler::compile(const Program &program) {
  BytecodeFunction func;
  func.name = "<script>";
  current_function_ = &func;

  for (const auto &stmt : program.body) {
    compile_stmt(*stmt);
  }

  current_function_ = nullptr;
  return func;
}

// --- Register allocation ---

uint8_t Compiler::allocate_register() {
  uint8_t reg = current_function_->register_count;
  current_function_->register_count++;
  if (trace_) {
    CompilerStep step;
    step.type = CompilerStep::Type::AllocRegister;
    step.register_id = reg;
    step.description = "R" + std::to_string(reg);
    trace_step(step);
  }
  return reg;
}

// --- Constant pool ---

uint16_t Compiler::add_constant(Value val) {
  // Deduplicate constants
  auto &constants = current_function_->constants;
  for (size_t i = 0; i < constants.size(); ++i) {
    const auto &existing = constants[i];
    if (val.is_number() && existing.is_number() &&
        val.as_number() == existing.as_number()) {
      return static_cast<uint16_t>(i);
    }
    if (val.is_string() && existing.is_string() &&
        val.as_string()->data() == existing.as_string()->data()) {
      return static_cast<uint16_t>(i);
    }
  }
  uint16_t idx = static_cast<uint16_t>(constants.size());
  constants.push_back(val);
  if (trace_) {
    CompilerStep step;
    step.type = CompilerStep::Type::AddConstant;
    step.constant_index = idx;
    step.description = "K" + std::to_string(idx) + " = " + val.to_debug_string();
    trace_step(step);
  }
  return idx;
}

// --- String constant helper ---

uint16_t Compiler::add_string_constant(const std::string &str) {
  std::u16string utf16;
  utf16.reserve(str.size());
  for (char ch : str) {
    utf16.push_back(static_cast<char16_t>(static_cast<unsigned char>(ch)));
  }
  auto *js_str = gc_.allocate<JsString>(std::move(utf16));
  return add_constant(Value::object(js_str));
}

// --- Emit helpers ---

void Compiler::emit(Instruction instr) {
  int idx = static_cast<int>(current_function_->code.size());
  current_function_->code.push_back(instr);
  if (trace_) {
    CompilerStep step;
    step.type = CompilerStep::Type::EmitInstruction;
    step.instruction_index = idx;
    step.description = std::string(opcode_name(instr.opcode()));
    trace_step(step);
  }
}

void Compiler::emit_abc(OpCode op, uint8_t a, uint8_t b, uint8_t c) {
  emit(Instruction::abc(op, a, b, c));
}

void Compiler::emit_abx(OpCode op, uint8_t a, uint16_t bx) {
  emit(Instruction::abx(op, a, bx));
}

void Compiler::emit_asbx(OpCode op, uint8_t a, int16_t sbx) {
  emit(Instruction::asbx(op, a, sbx));
}

size_t Compiler::emit_jump(OpCode op, uint8_t a) {
  size_t index = current_function_->code.size();
  emit_asbx(op, a, 0); // placeholder offset
  return index;
}

void Compiler::patch_jump(size_t index) {
  patch_jump_to(index, current_function_->code.size());
}

void Compiler::patch_jump_to(size_t index, size_t target) {
  int16_t offset = static_cast<int16_t>(target - index - 1);
  auto &instr = current_function_->code[index];
  // Rebuild the instruction preserving opcode and A field, replacing sBx
  instr = Instruction::asbx(instr.opcode(), instr.a(), offset);
  if (trace_) {
    CompilerStep step;
    step.type = CompilerStep::Type::PatchJump;
    step.instruction_index = static_cast<int>(index);
    step.patch_target = static_cast<int>(target);
    step.description = "patch [" + std::to_string(index) + "] -> " + std::to_string(target);
    trace_step(step);
  }
}

size_t Compiler::current_offset() { return current_function_->code.size(); }

// --- TokenKind to OpCode mapping ---

static OpCode binary_op(TokenKind kind) {
  switch (kind) {
  case TokenKind::Plus:
    return OpCode::Add;
  case TokenKind::Minus:
    return OpCode::Sub;
  case TokenKind::Star:
    return OpCode::Mul;
  case TokenKind::Slash:
    return OpCode::Div;
  case TokenKind::Percent:
    return OpCode::Mod;
  case TokenKind::StarStar:
    return OpCode::Pow;
  case TokenKind::Ampersand:
    return OpCode::BitAnd;
  case TokenKind::Pipe:
    return OpCode::BitOr;
  case TokenKind::Caret:
    return OpCode::BitXor;
  case TokenKind::LessLess:
    return OpCode::ShiftLeft;
  case TokenKind::GreaterGreater:
    return OpCode::ShiftRight;
  case TokenKind::GreaterGreaterGreater:
    return OpCode::ShiftRightU;
  case TokenKind::EqualEqual:
    return OpCode::Equal;
  case TokenKind::BangEqual:
    return OpCode::NotEqual;
  case TokenKind::EqualEqualEqual:
    return OpCode::StrictEqual;
  case TokenKind::BangEqualEqual:
    return OpCode::StrictNotEqual;
  case TokenKind::Less:
    return OpCode::LessThan;
  case TokenKind::LessEqual:
    return OpCode::LessEqual;
  case TokenKind::Greater:
    return OpCode::GreaterThan;
  case TokenKind::GreaterEqual:
    return OpCode::GreaterEqual;
  default:
    return OpCode::Add; // unreachable for valid AST
  }
}

// --- Statement compilation ---

void Compiler::compile_stmt(const Stmt &stmt) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
          TraceNode tn(trace_, &trace_depth_, "ExpressionStmt", "");
          compile_expr(*node.expression);
          // Result register is discarded

        } else if constexpr (std::is_same_v<T, VarDeclaration>) {
          TraceNode tn(trace_, &trace_depth_, "VarDeclaration", node.name);
          if (node.initializer) {
            uint8_t val_reg = compile_expr(*node.initializer);
            uint16_t name_idx = add_string_constant(node.name);
            emit_abx(OpCode::SetGlobal, val_reg, name_idx);
          } else {
            uint8_t reg = allocate_register();
            emit_abc(OpCode::LoadUndef, reg, 0, 0);
            uint16_t name_idx = add_string_constant(node.name);
            emit_abx(OpCode::SetGlobal, reg, name_idx);
          }

        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          TraceNode tn(trace_, &trace_depth_, "BlockStmt", "");
          for (const auto &s : node.statements) {
            compile_stmt(*s);
          }

        } else if constexpr (std::is_same_v<T, IfStmt>) {
          TraceNode tn(trace_, &trace_depth_, "IfStmt", "");
          uint8_t cond_reg = compile_expr(*node.condition);
          size_t jump_else = emit_jump(OpCode::JumpIfFalse, cond_reg);
          compile_stmt(*node.consequent);
          if (node.alternate) {
            size_t jump_end = emit_jump(OpCode::Jump);
            patch_jump(jump_else);
            compile_stmt(*node.alternate);
            patch_jump(jump_end);
          } else {
            patch_jump(jump_else);
          }
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
          TraceNode tn(trace_, &trace_depth_, "WhileStmt", "");
          size_t loop_start = current_offset();
          loop_stack_.push_back(LoopContext{loop_start, {}, {}, false});
          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::PushLoop;
            step.description = "while loop";
            trace_step(step);
          }

          uint8_t cond_reg = compile_expr(*node.condition);
          size_t exit_jump = emit_jump(OpCode::JumpIfFalse, cond_reg);
          compile_stmt(*node.body);

          // Jump back to loop_start
          size_t back_jump_idx = current_offset();
          int16_t back_offset =
              static_cast<int16_t>(loop_start - back_jump_idx - 1);
          emit_asbx(OpCode::Jump, 0, back_offset);
          patch_jump(exit_jump);

          // Patch break jumps to here (after exit)
          auto &ctx = loop_stack_.back();

          for (size_t idx : ctx.break_jumps)
            patch_jump(idx);
          loop_stack_.pop_back();
          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::PopLoop;
            step.description = "while loop";
            trace_step(step);
          }
        } else if constexpr (std::is_same_v<T, ForStmt>) {
          TraceNode tn(trace_, &trace_depth_, "ForStmt", "");
          // Compile initializer
          if (node.init)
            compile_stmt(*node.init);

          size_t loop_start = current_offset();
          loop_stack_.push_back(LoopContext{0, {}, {}, true});
          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::PushLoop;
            step.description = "for loop";
            trace_step(step);
          }

          // Compile condition (if present)
          size_t exit_jump = 0;
          bool has_condition = node.condition != nullptr;
          if (has_condition) {
            uint8_t cond_reg = compile_expr(*node.condition);
            exit_jump = emit_jump(OpCode::JumpIfFalse, cond_reg);
          }

          // Compile body
          compile_stmt(*node.body);

          // Patch continue jumps to the update expression
          size_t update_start = current_offset();
          auto &ctx = loop_stack_.back();
          ctx.continue_target = update_start;
          for (size_t idx : ctx.continue_jumps) {
            patch_jump_to(idx, update_start);
          }

          // Compile update
          if (node.update)
            compile_expr(*node.update);

          // Jump back to loop_start
          size_t back_jump_idx = current_offset();
          int16_t back_offset =
              static_cast<int16_t>(loop_start - back_jump_idx - 1);
          emit_asbx(OpCode::Jump, 0, back_offset);

          if (has_condition)
            patch_jump(exit_jump);

          // Patch break jumps to here (after exit)
          for (size_t idx : ctx.break_jumps)
            patch_jump(idx);
          loop_stack_.pop_back();
          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::PopLoop;
            step.description = "for loop";
            trace_step(step);
          }

        } else if constexpr (std::is_same_v<T, BreakStmt>) {
          TraceNode tn(trace_, &trace_depth_, "BreakStmt", "");
          if (!loop_stack_.empty()) {
            size_t jump_idx = emit_jump(OpCode::Jump);
            loop_stack_.back().break_jumps.push_back(jump_idx);
          }

        } else if constexpr (std::is_same_v<T, ContinueStmt>) {
          TraceNode tn(trace_, &trace_depth_, "ContinueStmt", "");
          if (!loop_stack_.empty()) {
            auto &ctx = loop_stack_.back();
            if (ctx.is_for_loop) {
              // Defer: we don't know the update position yet
              size_t jump_idx = emit_jump(OpCode::Jump);
              ctx.continue_jumps.push_back(jump_idx);
            } else {
              // While loop: jump back to condition
              size_t jump_idx = current_offset();
              int16_t offset =
                  static_cast<int16_t>(ctx.continue_target - jump_idx - 1);
              emit_asbx(OpCode::Jump, 0, offset);
            }
          }
        } else if constexpr (std::is_same_v<T, FunctionDecl>) {
          TraceNode tn(trace_, &trace_depth_, "FunctionDecl", node.name);
          std::cerr << "warning: FunctionDecl not yet compiled\n";
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          TraceNode tn(trace_, &trace_depth_, "ReturnStmt", "");
          std::cerr << "warning: ReturnStmt not yet compiled\n";
        }
      },
      static_cast<const Stmt::variant &>(stmt));
}

// --- Expression compilation ---

uint8_t Compiler::compile_expr(const Expr &expr) {
  uint8_t dest = 0;

  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "NumberLiteral", expr_node_desc(expr));
          dest = allocate_register();
          uint16_t idx = add_constant(Value::number(node.value));
          emit_abx(OpCode::LoadConst, dest, idx);

        } else if constexpr (std::is_same_v<T, StringLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "StringLiteral", "\"" + node.value + "\"");
          dest = allocate_register();
          uint16_t idx = add_string_constant(node.value);
          emit_abx(OpCode::LoadConst, dest, idx);

        } else if constexpr (std::is_same_v<T, BooleanLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "BooleanLiteral", node.value ? "true" : "false");
          dest = allocate_register();
          emit_abc(node.value ? OpCode::LoadTrue : OpCode::LoadFalse, dest, 0,
                   0);

        } else if constexpr (std::is_same_v<T, NullLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "NullLiteral", "");
          dest = allocate_register();
          emit_abc(OpCode::LoadNull, dest, 0, 0);

        } else if constexpr (std::is_same_v<T, UndefinedLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "UndefinedLiteral", "");
          dest = allocate_register();
          emit_abc(OpCode::LoadUndef, dest, 0, 0);

        } else if constexpr (std::is_same_v<T, Identifier>) {
          TraceNode tn(trace_, &trace_depth_, "Identifier", node.name);
          dest = allocate_register();
          uint16_t name_idx = add_string_constant(node.name);
          emit_abx(OpCode::GetGlobal, dest, name_idx);

        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          TraceNode tn(trace_, &trace_depth_, "BinaryExpr", std::string(token_kind_to_string(node.op)));
          if (node.op == TokenKind::AmpersandAmpersand) {
            // a && b: short-circuit — if a is falsy, result is a; otherwise b
            uint8_t left = compile_expr(*node.left);
            dest = allocate_register();
            emit_abc(OpCode::Move, dest, left, 0);
            size_t skip_jump = emit_jump(OpCode::JumpIfFalse, dest);
            uint8_t right = compile_expr(*node.right);
            emit_abc(OpCode::Move, dest, right, 0);
            patch_jump(skip_jump);
          } else if (node.op == TokenKind::PipePipe) {
            // a || b: short-circuit — if a is truthy, result is a; otherwise b
            uint8_t left = compile_expr(*node.left);
            dest = allocate_register();
            emit_abc(OpCode::Move, dest, left, 0);
            size_t skip_jump = emit_jump(OpCode::JumpIfTrue, dest);
            uint8_t right = compile_expr(*node.right);
            emit_abc(OpCode::Move, dest, right, 0);
            patch_jump(skip_jump);
          } else {
            uint8_t left = compile_expr(*node.left);
            uint8_t right = compile_expr(*node.right);
            dest = allocate_register();
            emit_abc(binary_op(node.op), dest, left, right);
          }

        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          TraceNode tn(trace_, &trace_depth_, "UnaryExpr", std::string(token_kind_to_string(node.op)));
          uint8_t operand = compile_expr(*node.operand);
          dest = allocate_register();
          if (node.op == TokenKind::Minus) {
            emit_abc(OpCode::Neg, dest, operand, 0);
          } else if (node.op == TokenKind::Bang) {
            emit_abc(OpCode::Not, dest, operand, 0);
          } else if (node.op == TokenKind::Tilde) {
            emit_abc(OpCode::BitNot, dest, operand, 0);
          } else if (node.op == TokenKind::TypeOf) {
            emit_abc(OpCode::TypeOf, dest, operand, 0);
          } else {
            std::cerr << "warning: unsupported unary operator\n";
          }

        } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
          TraceNode tn(trace_, &trace_depth_, "AssignmentExpr", std::string(token_kind_to_string(node.op)));
          // Only simple assignment to identifiers for now
          if (auto *ident = std::get_if<Identifier>(&*node.target)) {
            uint8_t val_reg = compile_expr(*node.value);
            uint16_t name_idx = add_string_constant(ident->name);
            emit_abx(OpCode::SetGlobal, val_reg, name_idx);
            dest = val_reg;
          } else {
            dest = allocate_register();
            std::cerr
                << "warning: complex assignment target not yet compiled\n";
          }

        } else if constexpr (std::is_same_v<T, CallExpr>) {
          TraceNode tn(trace_, &trace_depth_, "CallExpr", "");
          // Check for console.log(...) pattern
          bool is_console_log = false;
          if (auto *member = std::get_if<MemberExpr>(&*node.callee)) {
            if (!member->is_computed && member->property == "log") {
              if (auto *obj = std::get_if<Identifier>(&*member->object)) {
                if (obj->name == "console") {
                  is_console_log = true;
                }
              }
            }
          }

          if (is_console_log) {
            // Compile each argument, then move results into consecutive regs
            std::vector<uint8_t> arg_regs;
            for (const auto &arg : node.arguments) {
              arg_regs.push_back(compile_expr(*arg));
            }
            // Allocate a consecutive block for Print
            uint8_t first_arg = current_function_->register_count;
            for (size_t i = 0; i < arg_regs.size(); ++i) {
              uint8_t slot = allocate_register();
              if (arg_regs[i] != slot) {
                emit_abc(OpCode::Move, slot, arg_regs[i], 0);
              }
            }
            uint8_t arg_count = static_cast<uint8_t>(arg_regs.size());
            emit_abc(OpCode::Print, first_arg, arg_count, 0);
            dest = allocate_register(); // result is undefined
            emit_abc(OpCode::LoadUndef, dest, 0, 0);
          } else {
            dest = allocate_register();
            std::cerr << "warning: CallExpr not yet compiled\n";
          }

        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          TraceNode tn(trace_, &trace_depth_, "MemberExpr", node.is_computed ? "[]" : "." + node.property);
          dest = allocate_register();
          std::cerr << "warning: MemberExpr not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "ArrayLiteral", "");
          dest = allocate_register();
          std::cerr << "warning: ArrayLiteral not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "ObjectLiteral", "");
          dest = allocate_register();
          std::cerr << "warning: ObjectLiteral not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ArrowFunction>) {
          TraceNode tn(trace_, &trace_depth_, "ArrowFunction", "");
          dest = allocate_register();
          std::cerr << "warning: ArrowFunction not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          TraceNode tn(trace_, &trace_depth_, "ConditionalExpr", "");
          dest = allocate_register();
          std::cerr << "warning: ConditionalExpr not yet compiled\n";

        } else if constexpr (std::is_same_v<T, TemplateLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "TemplateLiteral", "");
          dest = allocate_register();
          std::cerr << "warning: TemplateLiteral not yet compiled\n";
        }
      },
      static_cast<const Expr::variant &>(expr));

  return dest;
}

} // namespace yatsi
