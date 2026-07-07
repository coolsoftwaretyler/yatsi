#include "compiler/compiler.h"

#include "compiler/bytecode.h"
#include "runtime/js_string.h"

#include <cstdint>
#include <iostream>

namespace yatsi {

// --- Tracing infrastructure ---

void Compiler::enable_tracing(std::vector<CompilerStep> &trace) {
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
  std::vector<CompilerStep> *trace_;
  size_t *depth_;
  std::string node_type_;
  int line_;
  int col_;

  TraceNode(std::vector<CompilerStep> *t, size_t *depth,
            const std::string &node_type, const std::string &detail,
            int line = -1, int col = -1)
      : trace_(t), depth_(depth), node_type_(node_type), line_(line),
        col_(col) {
    if (trace_) {
      CompilerStep step;
      step.type = CompilerStep::Type::EnterNode;
      step.depth = *depth_;
      step.node_type = node_type;
      step.description = detail;
      step.source_line = line;
      step.source_column = col;
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
      step.source_line = line_;
      step.source_column = col_;
      trace_->push_back(std::move(step));
    }
  }
};

static std::string expr_node_desc(const Expr &e) {
  return std::visit(
      [](const auto &v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NumberLiteral>) {
          if (v.value == static_cast<int64_t>(v.value))
            return "value=" + std::to_string(static_cast<int64_t>(v.value));
          return "value=" + std::to_string(v.value);
        } else if constexpr (std::is_same_v<T, StringLiteral>)
          return "\"" + v.value + "\"";
        else if constexpr (std::is_same_v<T, BooleanLiteral>)
          return v.value ? "true" : "false";
        else if constexpr (std::is_same_v<T, NullLiteral>)
          return "null";
        else if constexpr (std::is_same_v<T, UndefinedLiteral>)
          return "undefined";
        else if constexpr (std::is_same_v<T, Identifier>)
          return v.name;
        else if constexpr (std::is_same_v<T, BinaryExpr>)
          return std::string(token_kind_to_string(v.op));
        else if constexpr (std::is_same_v<T, UnaryExpr>)
          return std::string(token_kind_to_string(v.op));
        else if constexpr (std::is_same_v<T, AssignmentExpr>)
          return std::string(token_kind_to_string(v.op));
        else if constexpr (std::is_same_v<T, CallExpr>)
          return "";
        else if constexpr (std::is_same_v<T, MemberExpr>)
          return v.is_computed ? "[]" : "." + v.property;
        else
          return "";
      },
      static_cast<const Expr::variant &>(e));
}

Compiler::Compiler(GarbageCollector &gc) : gc_(gc) {}

// Scope management
void Compiler::begin_scope() { scope_depth_++; }

void Compiler::end_scope() {
  scope_depth_--;
  // Remove any locals that belong to the scope we're exiting
  while (!locals_.empty() && locals_.back().depth > scope_depth_) {
    locals_.pop_back();
  }
}

int Compiler::resolve_local(const std::string &name) {
  for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; --i) {
    if (locals_[i].name == name) {
      if (trace_) {
        CompilerStep step;
        step.type = CompilerStep::Type::ResolveLocal;
        step.variable_name = name;
        step.register_id = locals_[i].reg;
        step.function_name = current_function_->name;
        step.description = "resolve_local('" + name + "') -> R" +
                           std::to_string(locals_[i].reg);
        trace_step(step);
      }
      return locals_[i].reg;
    }
  }
  if (trace_) {
    CompilerStep step;
    step.type = CompilerStep::Type::ResolveLocalNotFound;
    step.variable_name = name;
    step.function_name = current_function_->name;
    step.description = "resolve_local('" + name + "') -> not found";
    trace_step(step);
  }
  return -1;
}

int Compiler::add_upvalue(uint8_t index, bool is_local) {
  // Check if we already have this upvalue
  for (int i = 0; i < static_cast<int>(upvalues_.size()); ++i) {
    if (upvalues_[i].index == index && upvalues_[i].is_local == is_local) {
      if (trace_) {
        CompilerStep step;
        step.type = CompilerStep::Type::UpvalueDedup;
        step.upvalue_index = i;
        step.is_local_upvalue = is_local;
        step.function_name = current_function_->name;
        step.description =
            "UV" + std::to_string(i) + " already exists, reusing";
        trace_step(step);
      }
      return i;
    }
  }
  upvalues_.push_back({index, is_local});
  int uv_idx = static_cast<int>(upvalues_.size()) - 1;
  if (trace_) {
    CompilerStep step;
    step.type = CompilerStep::Type::AddUpvalue;
    step.upvalue_index = uv_idx;
    step.is_local_upvalue = is_local;
    step.function_name = current_function_->name;
    step.description =
        "UV" + std::to_string(uv_idx) + (is_local ? " (local)" : " (upvalue)");
    trace_step(step);
  }
  return uv_idx;
}

int Compiler::resolve_upvalue(const std::string &name) {
  if (!enclosing_)
    return -1;

  // Check if the variable is a local in the enclosing function
  for (int i = static_cast<int>(enclosing_->locals.size()) - 1; i >= 0; --i) {
    if (enclosing_->locals[i].name == name) {
      enclosing_->locals[i].is_captured = true;
      if (trace_) {
        CompilerStep step;
        step.type = CompilerStep::Type::MarkCaptured;
        step.variable_name = name;
        step.register_id = enclosing_->locals[i].reg;
        step.function_name = enclosing_->function->name;
        step.description = "mark '" + name + "' (R" +
                           std::to_string(enclosing_->locals[i].reg) +
                           ") as captured";
        trace_step(step);
      }
      int result = add_upvalue(enclosing_->locals[i].reg, true);
      if (trace_) {
        CompilerStep step;
        step.type = CompilerStep::Type::ResolveUpvalue;
        step.variable_name = name;
        step.upvalue_index = result;
        step.is_local_upvalue = true;
        step.function_name = enclosing_->function->name;
        step.description = "resolve '" + name + "' -> UV" +
                           std::to_string(result) + " (local from " +
                           enclosing_->function->name + ")";
        trace_step(step);
      }
      return result;
    }
  }

  // Check if it's an upvalue in the enclosing function (recursive)
  // Temproarily restore enclosing state to revove further up
  auto *saved_enclosing = enclosing_;
  auto saved_upvalues = std::move(upvalues_);

  // Restore enclosing's context
  upvalues_ = std::move(enclosing_->upvalues);
  enclosing_ = enclosing_->enclosing;

  int upvalue_idx = resolve_upvalue(name);

  // Save back what enclosing resolved
  saved_enclosing->upvalues = std::move(upvalues_);
  saved_enclosing->enclosing = enclosing_;

  // Restore our state
  upvalues_ = std::move(saved_upvalues);
  enclosing_ = saved_enclosing;

  if (upvalue_idx >= 0) {
    int result = add_upvalue(static_cast<uint8_t>(upvalue_idx), false);
    if (trace_) {
      CompilerStep step;
      step.type = CompilerStep::Type::ResolveUpvalue;
      step.variable_name = name;
      step.upvalue_index = result;
      step.is_local_upvalue = false;
      step.function_name = current_function_->name;
      step.description = "resolve '" + name + "' -> UV" +
                         std::to_string(result) + " (chained through " +
                         enclosing_->function->name + ")";
      trace_step(step);
    }
    return result;
  }

  return -1;
}

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
    step.function_name = current_function_->name;
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
    step.description =
        "K" + std::to_string(idx) + " = " + val.to_debug_string();
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
    step.description =
        "patch [" + std::to_string(index) + "] -> " + std::to_string(target);
    trace_step(step);
  }
}

size_t Compiler::current_offset() { return current_function_->code.size(); }

static bool is_known_number(const Expr &expr) {
  return expr.resolved_type.has_value() && is_number_type(*expr.resolved_type);
}

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

static OpCode binary_op_num(TokenKind kind) {
  switch (kind) {
  case TokenKind::Plus:
    return OpCode::AddNum;
  case TokenKind::Minus:
    return OpCode::SubNum;
  case TokenKind::Star:
    return OpCode::MulNum;
  case TokenKind::Slash:
    return OpCode::DivNum;
  case TokenKind::Percent:
    return OpCode::ModNum;
  case TokenKind::StarStar:
    return OpCode::PowNum;
  default:
    return binary_op(kind);
  }
}

// --- Statement compilation ---

void Compiler::compile_stmt(const Stmt &stmt) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
          TraceNode tn(trace_, &trace_depth_, "ExpressionStmt", "",
                       stmt.location.line, stmt.location.column);
          compile_expr(*node.expression);
          // Result register is discarded

        } else if constexpr (std::is_same_v<T, VarDeclaration>) {
          TraceNode tn(trace_, &trace_depth_, "VarDeclaration", node.name,
                       stmt.location.line, stmt.location.column);
          if (scope_depth_ > 0) {
            uint8_t reg;
            if (node.initializer) {
              reg = compile_expr(*node.initializer);
            } else {
              reg = allocate_register();
              emit_abc(OpCode::LoadUndef, reg, 0, 0);
            }
            locals_.push_back({node.name, reg, scope_depth_});
          } else {
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
          }

        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          TraceNode tn(trace_, &trace_depth_, "BlockStmt", "",
                       stmt.location.line, stmt.location.column);
          for (const auto &s : node.statements) {
            compile_stmt(*s);
          }

        } else if constexpr (std::is_same_v<T, IfStmt>) {
          TraceNode tn(trace_, &trace_depth_, "IfStmt", "", stmt.location.line,
                       stmt.location.column);
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
          TraceNode tn(trace_, &trace_depth_, "WhileStmt", "",
                       stmt.location.line, stmt.location.column);
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
          TraceNode tn(trace_, &trace_depth_, "ForStmt", "", stmt.location.line,
                       stmt.location.column);
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
          TraceNode tn(trace_, &trace_depth_, "BreakStmt", "",
                       stmt.location.line, stmt.location.column);
          if (!loop_stack_.empty()) {
            size_t jump_idx = emit_jump(OpCode::Jump);
            loop_stack_.back().break_jumps.push_back(jump_idx);
          }

        } else if constexpr (std::is_same_v<T, ContinueStmt>) {
          TraceNode tn(trace_, &trace_depth_, "ContinueStmt", "",
                       stmt.location.line, stmt.location.column);
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
          TraceNode tn(trace_, &trace_depth_, "FunctionDecl", node.name,
                       stmt.location.line, stmt.location.column);
          // Save compiler state and set up an enclosing state for closures
          EnclosingState enclosing;
          enclosing.function = current_function_;
          enclosing.locals = std::move(locals_);
          enclosing.upvalues = std::move(upvalues_);
          enclosing.scope_depth = scope_depth_;
          enclosing.enclosing = enclosing_;

          // Then we create a new BytecodeFunction to represent this function as
          // a child, and we wire it up with the node's names. We intitialize
          // the current function, scope depth, and locals stack
          BytecodeFunction child;
          child.name = node.name;
          // Note, we should eventually warn or error if a function has more
          // than 256 args
          child.param_count = static_cast<uint8_t>(node.params.size());
          current_function_ = &child;
          scope_depth_ = 1;
          locals_.clear();
          upvalues_.clear();
          enclosing_ = &enclosing;

          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::EnterFunction;
            step.function_name = child.name;
            step.param_count = static_cast<int>(node.params.size());
            step.description = "enter function '" + child.name + "' (" +
                               std::to_string(node.params.size()) + " params)";
            trace_step(step);
          }

          // Register the parameters as locals in consecutive order her
          for (const auto &param : node.params) {
            uint8_t reg = allocate_register();
            locals_.push_back({param.name, reg, scope_depth_});
          }

          // We compile the body of the function
          if (auto *block = std::get_if<BlockStmt>(&*node.body)) {
            for (const auto &s : block->statements) {
              compile_stmt(*s);
            }
          }

          // after compilation, return undefined. This is default behavior, and
          // eventually we will support other return types.
          emit_abc(OpCode::ReturnUndef, 0, 0, 0);

          // Copy upvalue descriptors into the child BytecodeFunction
          for (const auto &uv : upvalues_) {
            child.upvalue_descs.push_back({uv.index, uv.is_local});
          }

          // Restore compiler state from before function compilation
          current_function_ = enclosing.function;
          locals_ = std::move(enclosing.locals);
          upvalues_ = std::move(enclosing.upvalues);
          scope_depth_ = enclosing.scope_depth;
          enclosing_ = enclosing.enclosing;

          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::ExitFunction;
            step.function_name = child.name;
            step.upvalue_count = static_cast<int>(child.upvalue_descs.size());
            step.description = "exit function '" + child.name + "' (" +
                               std::to_string(child.upvalue_descs.size()) +
                               " upvalues)";
            trace_step(step);
          }

          // Add child to parent's functions vector
          uint16_t func_index =
              static_cast<uint16_t>(current_function_->functions.size());
          current_function_->functions.push_back(std::move(child));

          // In parent: emit Closure + SetGlobal
          uint8_t dest = allocate_register();
          emit_abx(OpCode::Closure, dest, func_index);
          uint16_t name_idx = add_string_constant(node.name);
          emit_abx(OpCode::SetGlobal, dest, name_idx);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          TraceNode tn(trace_, &trace_depth_, "ReturnStmt", "",
                       stmt.location.line, stmt.location.column);
          if (node.value) {
            uint8_t val_reg = compile_expr(*node.value);
            emit_abc(OpCode::Return, val_reg, 0, 0);
          } else {
            emit_abc(OpCode::ReturnUndef, 0, 0, 0);
          }
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
          TraceNode tn(trace_, &trace_depth_, "NumberLiteral",
                       expr_node_desc(expr), expr.location.line,
                       expr.location.column);
          dest = allocate_register();
          uint16_t idx = add_constant(Value::number(node.value));
          emit_abx(OpCode::LoadConst, dest, idx);

        } else if constexpr (std::is_same_v<T, StringLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "StringLiteral",
                       "\"" + node.value + "\"", expr.location.line,
                       expr.location.column);
          dest = allocate_register();
          uint16_t idx = add_string_constant(node.value);
          emit_abx(OpCode::LoadConst, dest, idx);

        } else if constexpr (std::is_same_v<T, BooleanLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "BooleanLiteral",
                       node.value ? "true" : "false", expr.location.line,
                       expr.location.column);
          dest = allocate_register();
          emit_abc(node.value ? OpCode::LoadTrue : OpCode::LoadFalse, dest, 0,
                   0);

        } else if constexpr (std::is_same_v<T, NullLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "NullLiteral", "",
                       expr.location.line, expr.location.column);
          dest = allocate_register();
          emit_abc(OpCode::LoadNull, dest, 0, 0);

        } else if constexpr (std::is_same_v<T, UndefinedLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "UndefinedLiteral", "",
                       expr.location.line, expr.location.column);
          dest = allocate_register();
          emit_abc(OpCode::LoadUndef, dest, 0, 0);

        } else if constexpr (std::is_same_v<T, Identifier>) {
          TraceNode tn(trace_, &trace_depth_, "Identifier", node.name,
                       expr.location.line, expr.location.column);
          // When resolving an identifier, we first attempt to find it in our
          // locals
          int local_reg = resolve_local(node.name);
          // If there's a local register for this name, we return that as the
          // destination
          if (local_reg >= 0) {
            dest = static_cast<uint8_t>(local_reg);
          } else {
            // If we can't find the name in locals, we start walking our
            // upvalues. Upvalues are what makes a "closure" - gives us the
            // ability to enclose around variables from outer scope.
            //
            // resolve_upvalue will return -1 if there is no enclosing scope,
            // or if we cannot find any upvalues through the chain
            //
            // The way we actually check the upvalues is by searching all of the
            // enclosing_.locals to see if any of them have the same name as
            // this identifier we're compiling. If we find one, we mark its
            // representative Local struct is_captured value as true, and then
            // we push the index of that local register into the upvalues_
            // vector. In this case (resolved in the *locals* of the enclosing
            // function), we mark it as is_local
            //
            // But if we do not find it in the enclosing locals,
            // we then recursively check further up into other enclosing
            // functions. If we find a match there, we add an upvalue, but mark
            // it as is_local false
            //
            // If we found an upvalue in locals or enclosuers, we emit a
            // GetUpvalue instruction, pointing to the index
            //
            // If none of that hits and we end up with `-1`, then we emit a
            // GetGlobal instruction, because that's our global fallback for
            // resolving values.
            int upvalue_idx = resolve_upvalue(node.name);
            if (upvalue_idx >= 0) {
              dest = allocate_register();
              emit_abc(OpCode::GetUpvalue, dest,
                       static_cast<uint8_t>(upvalue_idx), 0);
            } else {
              if (trace_) {
                CompilerStep step;
                step.type = CompilerStep::Type::ResolveGlobal;
                step.variable_name = node.name;
                step.description =
                    "resolve '" + node.name + "' -> global (not in locals" +
                    std::string(enclosing_ ? " or upvalues"
                                           : ", no enclosing scope") +
                    ")";
                trace_step(step);
              }
              dest = allocate_register();
              uint16_t name_idx = add_string_constant(node.name);
              emit_abx(OpCode::GetGlobal, dest, name_idx);
            }
          }

        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          TraceNode tn(trace_, &trace_depth_, "BinaryExpr",
                       std::string(token_kind_to_string(node.op)),
                       expr.location.line, expr.location.column);
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
            if (is_known_number(*node.left) && is_known_number(*node.right)) {
              emit_abc(binary_op_num(node.op), dest, left, right);
            } else {
              emit_abc(binary_op(node.op), dest, left, right);
            }
          }

        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          TraceNode tn(trace_, &trace_depth_, "UnaryExpr",
                       std::string(token_kind_to_string(node.op)),
                       expr.location.line, expr.location.column);
          uint8_t operand = compile_expr(*node.operand);
          dest = allocate_register();
          if (node.op == TokenKind::Minus) {
            if (is_known_number(*node.operand)) {
              emit_abc(OpCode::NegNum, dest, operand, 0);
            } else {
              emit_abc(OpCode::Neg, dest, operand, 0);
            }
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
          TraceNode tn(trace_, &trace_depth_, "AssignmentExpr",
                       std::string(token_kind_to_string(node.op)),
                       expr.location.line, expr.location.column);
          if (auto *ident = std::get_if<Identifier>(&*node.target)) {
            int local_reg = resolve_local(ident->name);
            if (local_reg >= 0) {
              uint8_t val_reg = compile_expr(*node.value);
              if (val_reg != static_cast<uint8_t>(local_reg)) {
                emit_abc(OpCode::Move, static_cast<uint8_t>(local_reg), val_reg,
                         0);
              }
              dest = static_cast<uint8_t>(local_reg);
            } else {
              int upvalue_idx = resolve_upvalue(ident->name);
              if (upvalue_idx >= 0) {
                uint8_t val_reg = compile_expr(*node.value);
                emit_abc(OpCode::SetUpvalue, val_reg,
                         static_cast<uint8_t>(upvalue_idx), 0);
                dest = val_reg;
              } else {
                if (trace_) {
                  CompilerStep step;
                  step.type = CompilerStep::Type::ResolveGlobal;
                  step.variable_name = ident->name;
                  step.description =
                      "resolve '" + ident->name + "' -> global (not in locals" +
                      std::string(enclosing_ ? " or upvalues"
                                             : ", no enclosing scope") +
                      ")";
                  trace_step(step);
                }
                uint8_t val_reg = compile_expr(*node.value);
                uint16_t name_idx = add_string_constant(ident->name);
                emit_abx(OpCode::SetGlobal, val_reg, name_idx);
                dest = val_reg;
              }
            }
          } else if (auto *member = std::get_if<MemberExpr>(&*node.target)) {
            // If we're in an assignment expression node and it's a MemberExpr,
            // obj['x'] = 10
            // obj.x = 10
            // First we need to compile the expression for the member
            uint8_t obj_reg = compile_expr(*member->object);
            // Then we need to compile the expression of the value we'll be
            // assigning
            uint8_t val_reg = compile_expr(*node.value);
            // If the member access is something like obj['x'], it's computed,
            // so we need to compile the bracket expression to get a register
            // holding the key value, then we use that register in SetIndex.
            // Then we emit SetIndex based on the object register, the index
            // into the compiled computed value, and the value
            if (member->is_computed) {
              uint8_t idx_reg = compile_expr(*member->computed);
              emit_abc(OpCode::SetIndex, obj_reg, idx_reg, val_reg);
            } else {
              // Otherwise, we are doing a dot notation like obj.x,
              // so we add a key index, then emit an opcode to set a prop,
              // where the A register is where the object is, the b register is
              // a uint8_t of the key index, and the C register is the value
              // we're setting
              uint16_t key_idx = add_string_constant(member->property);
              emit_abc(OpCode::SetProp, obj_reg, static_cast<uint8_t>(key_idx),
                       val_reg);
            }
            // Finally, we make sure to put the destination as the value
            // register
            dest = val_reg;
          } else {
            dest = allocate_register();
            std::cerr
                << "warning: complex assignment target not yet compiled\n";
          }

        } else if constexpr (std::is_same_v<T, CallExpr>) {
          TraceNode tn(trace_, &trace_depth_, "CallExpr", "",
                       expr.location.line, expr.location.column);
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
            // General function call
            // Compile callee into a register
            uint8_t callee_reg = compile_expr(*node.callee);

            // Compile arguments into registers immediately after callee
            // We need them consecutive: R[callee], R[callee+1], ...,
            // R[callee+N] But compile_expr allocates monotonically, so we
            // compile args then move everything into a consecutive block.
            // TODO: this is a good spot for a visualization, IMO.
            std::vector<uint8_t> arg_regs;
            for (const auto &arg : node.arguments) {
              arg_regs.push_back(compile_expr(*arg));
            }

            // Allocate a consecutive block: [callee_slot, arg1, arg2, ...]
            uint8_t callee_slot = allocate_register();
            if (callee_reg != callee_slot) {
              emit_abc(OpCode::Move, callee_slot, callee_reg, 0);
            }
            for (size_t i = 0; i < arg_regs.size(); ++i) {
              uint8_t slot = allocate_register();
              if (arg_regs[i] != slot) {
                emit_abc(OpCode::Move, slot, arg_regs[i], 0);
              }
            }

            uint8_t arg_count = static_cast<uint8_t>(arg_regs.size());
            emit_abc(OpCode::Call, callee_slot, arg_count, 0);
            // Return value is placed in callee_slot by the VM
            dest = callee_slot;
          }

        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          TraceNode tn(trace_, &trace_depth_, "MemberExpr",
                       node.is_computed ? "[]" : "." + node.property,
                       expr.location.line, expr.location.column);
          // First we need to compile the object itself
          uint8_t obj_reg = compile_expr(*node.object);
          // Then we allocate a destination register for the member access to
          // point to
          dest = allocate_register();
          // We either want to get a property (i.e. dot notation),
          // or we need to get an index based on a computed expression.
          // So first we check to see if the AST node has `is_computed = true`
          if (node.is_computed) {
            // If it's computed,w e need to compile the computed part of the
            // node
            uint8_t idx_reg = compile_expr(*node.computed);
            emit_abc(OpCode::GetIndex, dest, obj_reg, idx_reg);
          } else {
            // Otherwise, we can just emit a GetProp with the node's value
            uint16_t key_idx = add_string_constant(node.property);
            emit_abc(OpCode::GetProp, dest, obj_reg,
                     static_cast<uint8_t>(key_idx));
          }

        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "ArrayLiteral", "",
                       expr.location.line, expr.location.column);
          // Allocate a register where we'll put the newly created array
          dest = allocate_register();
          // Emit an opcode for NewArray, with the a operand pointing to dest. B
          // and C are 0, won't be used
          emit_abc(OpCode::NewArray, dest, 0, 0);

          // Loop through all the elements provided in the AST node,
          // compile them, and emit SetIndex opcodes for each one
          // At each index, we allocate a register for the index constant and load it,
          // to keep SetIndex the same across objects and arrays.
          // TODO; we can probably optimize by adding an array-only opcode.
          for (size_t i = 0; i < node.elements.size(); ++i) {
            uint8_t val_reg = compile_expr(*node.elements[i]);
            uint8_t idx_reg = allocate_register();
            uint16_t idx_const =
                add_constant(Value::number(static_cast<double>(i)));
            emit_abx(OpCode::LoadConst, idx_reg, idx_const);
            emit_abc(OpCode::SetIndex, dest, idx_reg, val_reg);
          }

        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "ObjectLiteral", "",
                       expr.location.line, expr.location.column);
          dest = allocate_register();
          emit_abc(OpCode::NewObject, dest, 0, 0);
          for (const auto &prop : node.properties) {
            // We need the key name to be a string, so allocate key_name as such
            std::string key_name;
            if (auto *ident = std::get_if<Identifier>(&*prop.key)) {
              // If the prop reference key is an Identifier, we need to use that
              // identifier's name field
              key_name = ident->name;
            } else if (auto *str = std::get_if<StringLiteral>(&*prop.key)) {
              // If the prop's key field is a string literal, we use its value
              key_name = str->value;
            }
            // Add a string constant to the constant pool for the key name
            // (derived from identifier->name or stringliteratl->value)
            uint16_t key_idx = add_string_constant(key_name);
            // Compile the expression of the property node's value
            uint8_t val_reg = compile_expr(*prop.value);
            // Emit an opcode to SetProp, where the value register is being
            // mapped to the key index in the destination register.
            emit_abc(OpCode::SetProp, dest, static_cast<uint8_t>(key_idx),
                     val_reg);
          }

        } else if constexpr (std::is_same_v<T, ArrowFunction>) {
          TraceNode tn(trace_, &trace_depth_, "ArrowFunction", "",
                       expr.location.line, expr.location.column);
          dest = allocate_register();
          // Save compiler state
          EnclosingState enclosing;
          enclosing.function = current_function_;
          enclosing.locals = std::move(locals_);
          enclosing.upvalues = std::move(upvalues_);
          enclosing.scope_depth = scope_depth_;
          enclosing.enclosing = enclosing_;
          BytecodeFunction child;
          child.name = "<arrow>";
          // TODO: check the param size somewhere since size_t can be larger
          // than uint8_t
          child.param_count = static_cast<uint8_t>(node.params.size());
          current_function_ = &child;
          scope_depth_ = 1;
          locals_.clear();
          upvalues_.clear();
          enclosing_ = &enclosing;

          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::EnterFunction;
            step.function_name = "<arrow>";
            step.param_count = static_cast<int>(node.params.size());
            step.description = "enter function '<arrow>' (" +
                               std::to_string(node.params.size()) + " params)";
            trace_step(step);
          }

          // Register parameters as locals
          for (const auto &param : node.params) {
            uint8_t r = allocate_register();
            locals_.push_back({param.name, r, scope_depth_});
          }

          // Compile body — expression or block
          if (auto *expr_body = std::get_if<ExprPtr>(&node.body)) {
            // Expression body: compile expr, emit Return
            uint8_t val = compile_expr(**expr_body);
            emit_abc(OpCode::Return, val, 0, 0);
          } else if (auto *stmt_body = std::get_if<StmtPtr>(&node.body)) {
            // Block body: compile block statements
            if (auto *block = std::get_if<BlockStmt>(&**stmt_body)) {
              for (const auto &s : block->statements) {
                compile_stmt(*s);
              }
            }
            // Implicit return undefined
            emit_abc(OpCode::ReturnUndef, 0, 0, 0);
          }

          for (const auto &uv : upvalues_) {
            child.upvalue_descs.push_back({uv.index, uv.is_local});
          }

          // Restore compiler state
          current_function_ = enclosing.function;
          locals_ = std::move(enclosing.locals);
          upvalues_ = std::move(enclosing.upvalues);
          scope_depth_ = enclosing.scope_depth;
          enclosing_ = enclosing.enclosing;

          if (trace_) {
            CompilerStep step;
            step.type = CompilerStep::Type::ExitFunction;
            step.function_name = "<arrow>";
            step.upvalue_count = static_cast<int>(child.upvalue_descs.size());
            step.description = "exit function '<arrow>' (" +
                               std::to_string(child.upvalue_descs.size()) +
                               " upvalues)";
            trace_step(step);
          }

          // Add child to parent and emit Closure
          uint16_t func_index =
              static_cast<uint16_t>(current_function_->functions.size());
          current_function_->functions.push_back(std::move(child));
          dest = allocate_register();
          emit_abx(OpCode::Closure, dest, func_index);

        } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          TraceNode tn(trace_, &trace_depth_, "ConditionalExpr", "",
                       expr.location.line, expr.location.column);
          dest = allocate_register();
          std::cerr << "warning: ConditionalExpr not yet compiled\n";

        } else if constexpr (std::is_same_v<T, TemplateLiteral>) {
          TraceNode tn(trace_, &trace_depth_, "TemplateLiteral", "",
                       expr.location.line, expr.location.column);
          dest = allocate_register();
          std::cerr << "warning: TemplateLiteral not yet compiled\n";
        }
      },
      static_cast<const Expr::variant &>(expr));

  return dest;
}

} // namespace yatsi
