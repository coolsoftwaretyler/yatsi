#include "compiler/compiler.h"

#include "runtime/js_string.h"

#include <iostream>

namespace yatsi {

Compiler::Compiler(GarbageCollector& gc) : gc_(gc) {}

BytecodeFunction Compiler::compile(const Program& program) {
  BytecodeFunction func;
  func.name = "<script>";
  current_function_ = &func;

  for (const auto& stmt : program.body) {
    compile_stmt(*stmt);
  }

  current_function_ = nullptr;
  return func;
}

// --- Register allocation ---

uint8_t Compiler::allocate_register() {
  uint8_t reg = current_function_->register_count;
  current_function_->register_count++;
  return reg;
}

// --- Constant pool ---

uint16_t Compiler::add_constant(Value val) {
  // Deduplicate number constants
  auto& constants = current_function_->constants;
  for (size_t i = 0; i < constants.size(); ++i) {
    const auto& existing = constants[i];
    if (val.is_number() && existing.is_number() &&
        val.as_number() == existing.as_number()) {
      return static_cast<uint16_t>(i);
    }
  }
  uint16_t idx = static_cast<uint16_t>(constants.size());
  constants.push_back(val);
  return idx;
}

// --- String constant helper ---

uint16_t Compiler::add_string_constant(const std::string& str) {
  std::u16string utf16;
  utf16.reserve(str.size());
  for (char ch : str) {
    utf16.push_back(static_cast<char16_t>(static_cast<unsigned char>(ch)));
  }
  auto* js_str = gc_.allocate<JsString>(std::move(utf16));
  return add_constant(Value::object(js_str));
}

// --- Emit helpers ---

void Compiler::emit(Instruction instr) {
  current_function_->code.push_back(instr);
}

void Compiler::emit_abc(OpCode op, uint8_t a, uint8_t b, uint8_t c) {
  emit(Instruction::abc(op, a, b, c));
}

void Compiler::emit_abx(OpCode op, uint8_t a, uint16_t bx) {
  emit(Instruction::abx(op, a, bx));
}

// --- TokenKind to OpCode mapping ---

static OpCode binary_op(TokenKind kind) {
  switch (kind) {
  case TokenKind::Plus: return OpCode::Add;
  case TokenKind::Minus: return OpCode::Sub;
  case TokenKind::Star: return OpCode::Mul;
  case TokenKind::Slash: return OpCode::Div;
  case TokenKind::Percent: return OpCode::Mod;
  case TokenKind::StarStar: return OpCode::Pow;
  case TokenKind::Ampersand: return OpCode::BitAnd;
  case TokenKind::Pipe: return OpCode::BitOr;
  case TokenKind::Caret: return OpCode::BitXor;
  case TokenKind::LessLess: return OpCode::ShiftLeft;
  case TokenKind::GreaterGreater: return OpCode::ShiftRight;
  case TokenKind::GreaterGreaterGreater: return OpCode::ShiftRightU;
  case TokenKind::EqualEqual: return OpCode::Equal;
  case TokenKind::BangEqual: return OpCode::NotEqual;
  case TokenKind::EqualEqualEqual: return OpCode::StrictEqual;
  case TokenKind::BangEqualEqual: return OpCode::StrictNotEqual;
  case TokenKind::Less: return OpCode::LessThan;
  case TokenKind::LessEqual: return OpCode::LessEqual;
  case TokenKind::Greater: return OpCode::GreaterThan;
  case TokenKind::GreaterEqual: return OpCode::GreaterEqual;
  default: return OpCode::Add; // unreachable for valid AST
  }
}

// --- Statement compilation ---

void Compiler::compile_stmt(const Stmt& stmt) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
          compile_expr(*node.expression);
          // Result register is discarded

        } else if constexpr (std::is_same_v<T, VarDeclaration>) {
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
          for (const auto& s : node.statements) {
            compile_stmt(*s);
          }

        } else if constexpr (std::is_same_v<T, IfStmt>) {
          std::cerr << "warning: IfStmt not yet compiled\n";
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
          std::cerr << "warning: WhileStmt not yet compiled\n";
        } else if constexpr (std::is_same_v<T, ForStmt>) {
          std::cerr << "warning: ForStmt not yet compiled\n";
        } else if constexpr (std::is_same_v<T, FunctionDecl>) {
          std::cerr << "warning: FunctionDecl not yet compiled\n";
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          std::cerr << "warning: ReturnStmt not yet compiled\n";
        }
      },
      static_cast<const Stmt::variant&>(stmt));
}

// --- Expression compilation ---

uint8_t Compiler::compile_expr(const Expr& expr) {
  uint8_t dest = 0;

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteral>) {
          dest = allocate_register();
          uint16_t idx = add_constant(Value::number(node.value));
          emit_abx(OpCode::LoadConst, dest, idx);

        } else if constexpr (std::is_same_v<T, StringLiteral>) {
          dest = allocate_register();
          uint16_t idx = add_string_constant(node.value);
          emit_abx(OpCode::LoadConst, dest, idx);

        } else if constexpr (std::is_same_v<T, BooleanLiteral>) {
          dest = allocate_register();
          emit_abc(node.value ? OpCode::LoadTrue : OpCode::LoadFalse, dest, 0,
                   0);

        } else if constexpr (std::is_same_v<T, NullLiteral>) {
          dest = allocate_register();
          emit_abc(OpCode::LoadNull, dest, 0, 0);

        } else if constexpr (std::is_same_v<T, UndefinedLiteral>) {
          dest = allocate_register();
          emit_abc(OpCode::LoadUndef, dest, 0, 0);

        } else if constexpr (std::is_same_v<T, Identifier>) {
          dest = allocate_register();
          uint16_t name_idx = add_string_constant(node.name);
          emit_abx(OpCode::GetGlobal, dest, name_idx);

        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          uint8_t left = compile_expr(*node.left);
          uint8_t right = compile_expr(*node.right);
          dest = allocate_register();
          emit_abc(binary_op(node.op), dest, left, right);

        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
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
          // Only simple assignment to identifiers for now
          if (auto* ident = std::get_if<Identifier>(&*node.target)) {
            uint8_t val_reg = compile_expr(*node.value);
            uint16_t name_idx = add_string_constant(ident->name);
            emit_abx(OpCode::SetGlobal, val_reg, name_idx);
            dest = val_reg;
          } else {
            dest = allocate_register();
            std::cerr << "warning: complex assignment target not yet compiled\n";
          }

        } else if constexpr (std::is_same_v<T, CallExpr>) {
          // Check for console.log(...) pattern
          bool is_console_log = false;
          if (auto* member = std::get_if<MemberExpr>(&*node.callee)) {
            if (!member->is_computed && member->property == "log") {
              if (auto* obj = std::get_if<Identifier>(&*member->object)) {
                if (obj->name == "console") {
                  is_console_log = true;
                }
              }
            }
          }

          if (is_console_log) {
            // Compile each argument, then move results into consecutive regs
            std::vector<uint8_t> arg_regs;
            for (const auto& arg : node.arguments) {
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
          dest = allocate_register();
          std::cerr << "warning: MemberExpr not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          dest = allocate_register();
          std::cerr << "warning: ArrayLiteral not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          dest = allocate_register();
          std::cerr << "warning: ObjectLiteral not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ArrowFunction>) {
          dest = allocate_register();
          std::cerr << "warning: ArrowFunction not yet compiled\n";

        } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          dest = allocate_register();
          std::cerr << "warning: ConditionalExpr not yet compiled\n";

        } else if constexpr (std::is_same_v<T, TemplateLiteral>) {
          dest = allocate_register();
          std::cerr << "warning: TemplateLiteral not yet compiled\n";
        }
      },
      static_cast<const Expr::variant&>(expr));

  return dest;
}

} // namespace yatsi
