#include "typechecker/checker.h"
#include "typechecker/types.h"

#include <sstream>

namespace yatsi {

// --- Tracing infrastructure ---

void TypeChecker::enable_tracing(std::vector<TypeCheckerStep> &trace) {
  trace_ = &trace;
}

void TypeChecker::trace_step(TypeCheckerStep step) {
  if (trace_) {
    step.depth = trace_depth_;
    trace_->push_back(std::move(step));
  }
}

void TypeChecker::capture_scope_snapshot(TypeCheckerStep &step) {
  step.scope_label = current_scope_label_;
  if (env_) {
    for (const auto &pair : env_->all_bindings()) {
      step.scope_bindings.emplace_back(pair.first, type_to_string(pair.second));
    }
  }
}

// Manual enter/exit helpers (no RAII — simpler for WASM)
void TypeChecker::trace_enter(bool is_expr, const std::string &node_type,
                              const std::string &detail, int line, int col) {
  if (!trace_) return;
  TypeCheckerStep step;
  step.type = is_expr ? TypeCheckerStep::Type::EnterExpr
                      : TypeCheckerStep::Type::EnterStmt;
  step.node_type = node_type;
  step.description = detail;
  step.source_line = line;
  step.source_column = col;
  trace_step(step);
  trace_depth_++;
}

void TypeChecker::trace_exit(bool is_expr, const std::string &node_type,
                             const std::string &type_result, int line,
                             int col) {
  if (!trace_) return;
  trace_depth_--;
  TypeCheckerStep step;
  step.type = is_expr ? TypeCheckerStep::Type::ExitExpr
                      : TypeCheckerStep::Type::ExitStmt;
  step.node_type = node_type;
  step.description = "done";
  step.type_result = type_result;
  step.source_line = line;
  step.source_column = col;
  trace_step(step);
}

// --- Public entry point ---

void TypeChecker::check(Program &program) {
  TypeEnvironment global;
  env_ = &global;
  current_scope_label_ = "global";

  for (auto &stmt : program.body) {
    check_stmt(*stmt);
  }

  env_ = nullptr;
}

// --- Annotation resolution ---

Type TypeChecker::resolve_annotation(const TypeAnnotation &annotation) {
  Type result;
  if (annotation.name == "number")
    result = Type{NumberType{}};
  else if (annotation.name == "string")
    result = Type{StringType{}};
  else if (annotation.name == "boolean")
    result = Type{BooleanType{}};
  else if (annotation.name == "void")
    result = Type{VoidType{}};
  else if (annotation.name == "null")
    result = Type{NullType{}};
  else if (annotation.name == "undefined")
    result = Type{UndefinedType{}};
  else
    result = Type{AnyType{}};

  if (trace_) {
    TypeCheckerStep step;
    step.type = TypeCheckerStep::Type::ResolveAnnotation;
    step.description =
        "\"" + annotation.name + "\" -> " + type_to_string(result);
    step.type_result = type_to_string(result);
    trace_step(step);
  }

  return result;
}

// --- Diagnostics ---

void TypeChecker::warn(const SourceLocation &loc, const std::string &message) {
  std::ostringstream oss;
  oss << loc.file << ":" << loc.line << ":" << loc.column
      << ": warning: " << message;
  warnings_.push_back(oss.str());

  if (trace_) {
    TypeCheckerStep step;
    step.type = TypeCheckerStep::Type::Warning;
    step.description = message;
    step.source_line = loc.line;
    step.source_column = loc.column;
    trace_step(step);
  }
}

// --- Statement checking ---

void TypeChecker::check_stmt(Stmt &stmt) {
  std::visit(
      [&](auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
          trace_enter(false, "ExpressionStmt", "", stmt.location.line,
                      stmt.location.column);
          check_expr(*node.expression);
          trace_exit(false, "ExpressionStmt", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, VarDeclaration>) {
          trace_enter(false, "VarDeclaration", node.name, stmt.location.line,
                      stmt.location.column);
          Type declared_type{AnyType{}};
          if (node.type_annotation) {
            declared_type = resolve_annotation(*node.type_annotation);
          }

          if (node.initializer) {
            Type init_type = check_expr(*node.initializer);

            if (node.type_annotation) {
              if (!is_any_type(declared_type) && !is_any_type(init_type) &&
                  type_to_string(declared_type) != type_to_string(init_type)) {
                warn(node.initializer->location,
                     "Type '" + type_to_string(init_type) +
                         "' is not assignable to type '" +
                         type_to_string(declared_type) + "'.");
              }
            } else {
              declared_type = init_type;
              if (trace_) {
                TypeCheckerStep step;
                step.type = TypeCheckerStep::Type::InferFromInitializer;
                step.variable_name = node.name;
                step.type_result = type_to_string(declared_type);
                step.description = node.name + " gets type " +
                                   type_to_string(declared_type) +
                                   " from initializer";
                trace_step(step);
              }
            }
          }

          env_->define(node.name, declared_type);

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::DefineVariable;
            step.variable_name = node.name;
            step.type_result = type_to_string(declared_type);
            step.description =
                "define " + node.name + ": " + type_to_string(declared_type);
            capture_scope_snapshot(step);
            trace_step(step);
          }

          trace_exit(false, "VarDeclaration", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          trace_enter(false, "BlockStmt", "", stmt.location.line,
                      stmt.location.column);

          TypeEnvironment block_env(env_);
          TypeEnvironment *saved = env_;
          std::string saved_label = current_scope_label_;
          env_ = &block_env;
          current_scope_label_ = "block";

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::EnterScope;
            step.scope_label = "block";
            step.description = "push block scope";
            capture_scope_snapshot(step);
            trace_step(step);
          }

          for (auto &s : node.statements) {
            check_stmt(*s);
          }

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::ExitScope;
            step.scope_label = "block";
            step.description = "pop block scope";
            capture_scope_snapshot(step);
            trace_step(step);
          }

          env_ = saved;
          current_scope_label_ = saved_label;

          trace_exit(false, "BlockStmt", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, IfStmt>) {
          trace_enter(false, "IfStmt", "", stmt.location.line,
                      stmt.location.column);
          check_expr(*node.condition);
          check_stmt(*node.consequent);
          if (node.alternate) {
            check_stmt(*node.alternate);
          }
          trace_exit(false, "IfStmt", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, WhileStmt>) {
          trace_enter(false, "WhileStmt", "", stmt.location.line,
                      stmt.location.column);
          check_expr(*node.condition);
          check_stmt(*node.body);
          trace_exit(false, "WhileStmt", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, ForStmt>) {
          trace_enter(false, "ForStmt", "", stmt.location.line,
                      stmt.location.column);

          TypeEnvironment for_env(env_);
          TypeEnvironment *saved = env_;
          std::string saved_label = current_scope_label_;
          env_ = &for_env;
          current_scope_label_ = "for";

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::EnterScope;
            step.scope_label = "for";
            step.description = "push for-loop scope";
            capture_scope_snapshot(step);
            trace_step(step);
          }

          if (node.init)
            check_stmt(*node.init);
          if (node.condition)
            check_expr(*node.condition);
          if (node.update)
            check_expr(*node.update);
          check_stmt(*node.body);

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::ExitScope;
            step.scope_label = "for";
            step.description = "pop for-loop scope";
            capture_scope_snapshot(step);
            trace_step(step);
          }

          env_ = saved;
          current_scope_label_ = saved_label;

          trace_exit(false, "ForStmt", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, FunctionDecl>) {
          trace_enter(false, "FunctionDecl", node.name, stmt.location.line,
                      stmt.location.column);

          Type fn_type = build_function_type(node.params, node.return_type);
          env_->define(node.name, fn_type);

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::DefineVariable;
            step.variable_name = node.name;
            step.type_result = type_to_string(fn_type);
            step.description =
                "define " + node.name + ": " + type_to_string(fn_type);
            capture_scope_snapshot(step);
            trace_step(step);
          }

          TypeEnvironment func_env(env_);
          TypeEnvironment *saved_env = env_;
          std::string saved_label = current_scope_label_;
          env_ = &func_env;
          current_scope_label_ = "function(" + node.name + ")";

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::EnterFunction;
            step.description = "enter function '" + node.name + "'";
            step.scope_label = current_scope_label_;
            step.variable_name = node.name;
            trace_step(step);
          }

          auto &ft = std::get<FunctionType>(fn_type);
          for (size_t i = 0; i < node.params.size(); ++i) {
            env_->define(node.params[i].name, *ft.param_types[i]);

            if (trace_) {
              TypeCheckerStep step;
              step.type = TypeCheckerStep::Type::DefineParam;
              step.variable_name = node.params[i].name;
              step.type_result = type_to_string(*ft.param_types[i]);
              step.description = "param " + node.params[i].name + ": " +
                                 type_to_string(*ft.param_types[i]);
              capture_scope_snapshot(step);
              trace_step(step);
            }
          }

          Type return_type = *ft.return_type;
          Type *saved_return_type = current_return_type_;
          current_return_type_ = &return_type;

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::SetReturnType;
            step.type_result = type_to_string(return_type);
            step.description =
                "expected return type: " + type_to_string(return_type);
            trace_step(step);
          }

          check_stmt(*node.body);

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::ExitFunction;
            step.description = "exit function '" + node.name + "'";
            step.variable_name = node.name;
            step.scope_label = current_scope_label_;
            capture_scope_snapshot(step);
            trace_step(step);
          }

          current_return_type_ = saved_return_type;
          env_ = saved_env;
          current_scope_label_ = saved_label;

          trace_exit(false, "FunctionDecl", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          trace_enter(false, "ReturnStmt", "", stmt.location.line,
                      stmt.location.column);
          if (node.value) {
            Type val_type = check_expr(*node.value);

            if (trace_) {
              TypeCheckerStep step;
              step.type = TypeCheckerStep::Type::CheckReturnValue;
              step.actual_type = type_to_string(val_type);
              step.expected_type = current_return_type_
                                       ? type_to_string(*current_return_type_)
                                       : "any";
              step.description = "return " + type_to_string(val_type) +
                                 " (expected " + step.expected_type + ")";
              trace_step(step);
            }

            if (current_return_type_ && !is_any_type(*current_return_type_) &&
                !is_any_type(val_type) &&
                type_to_string(*current_return_type_) !=
                    type_to_string(val_type)) {
              warn(node.value->location,
                   "Type '" + type_to_string(val_type) +
                       "' is not assignable to type '" +
                       type_to_string(*current_return_type_) + "'.");
            }
          } else {
            if (trace_) {
              TypeCheckerStep step;
              step.type = TypeCheckerStep::Type::CheckReturnValue;
              step.actual_type = "void";
              step.expected_type = current_return_type_
                                       ? type_to_string(*current_return_type_)
                                       : "any";
              step.description =
                  "return void (expected " + step.expected_type + ")";
              trace_step(step);
            }

            if (current_return_type_ && !is_any_type(*current_return_type_) &&
                !is_void_type(*current_return_type_)) {
              warn(stmt.location,
                   "Type 'void' is not assignable to type '" +
                       type_to_string(*current_return_type_) + "'.");
            }
          }
          trace_exit(false, "ReturnStmt", "", stmt.location.line,
                     stmt.location.column);

        } else if constexpr (std::is_same_v<T, BreakStmt>) {
          // Nothing to check
        } else if constexpr (std::is_same_v<T, ContinueStmt>) {
          // Nothing to check
        }
      },
      static_cast<Stmt::variant &>(stmt));
}

// --- Expression checking ---

Type TypeChecker::check_expr(Expr &expr) {
  Type result{AnyType{}};

  std::visit(
      [&](auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteral>) {
          trace_enter(true, "NumberLiteral", "", expr.location.line,
                      expr.location.column);
          result = Type{NumberType{}};
          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::InferFromLiteral;
            step.type_result = "number";
            step.description = "number literal -> number";
            step.source_line = expr.location.line;
            step.source_column = expr.location.column;
            trace_step(step);
          }
          trace_exit(true, "NumberLiteral", "number", expr.location.line,
                     expr.location.column);

        } else if constexpr (std::is_same_v<T, StringLiteral>) {
          trace_enter(true, "StringLiteral", "\"" + node.value + "\"",
                      expr.location.line, expr.location.column);
          result = Type{StringType{}};
          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::InferFromLiteral;
            step.type_result = "string";
            step.description = "string literal -> string";
            step.source_line = expr.location.line;
            step.source_column = expr.location.column;
            trace_step(step);
          }
          trace_exit(true, "StringLiteral", "string", expr.location.line,
                     expr.location.column);

        } else if constexpr (std::is_same_v<T, BooleanLiteral>) {
          trace_enter(true, "BooleanLiteral",
                      node.value ? "true" : "false", expr.location.line,
                      expr.location.column);
          result = Type{BooleanType{}};
          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::InferFromLiteral;
            step.type_result = "boolean";
            step.description = "boolean literal -> boolean";
            step.source_line = expr.location.line;
            step.source_column = expr.location.column;
            trace_step(step);
          }
          trace_exit(true, "BooleanLiteral", "boolean", expr.location.line,
                     expr.location.column);

        } else if constexpr (std::is_same_v<T, NullLiteral>) {
          trace_enter(true, "NullLiteral", "", expr.location.line,
                      expr.location.column);
          result = Type{NullType{}};
          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::InferFromLiteral;
            step.type_result = "null";
            step.description = "null literal -> null";
            step.source_line = expr.location.line;
            step.source_column = expr.location.column;
            trace_step(step);
          }
          trace_exit(true, "NullLiteral", "null", expr.location.line,
                     expr.location.column);

        } else if constexpr (std::is_same_v<T, UndefinedLiteral>) {
          trace_enter(true, "UndefinedLiteral", "", expr.location.line,
                      expr.location.column);
          result = Type{UndefinedType{}};
          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::InferFromLiteral;
            step.type_result = "undefined";
            step.description = "undefined literal -> undefined";
            step.source_line = expr.location.line;
            step.source_column = expr.location.column;
            trace_step(step);
          }
          trace_exit(true, "UndefinedLiteral", "undefined",
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, Identifier>) {
          trace_enter(true, "Identifier", node.name, expr.location.line,
                      expr.location.column);
          auto found = env_->lookup(node.name);
          if (found) {
            result = *found;
            if (trace_) {
              TypeCheckerStep step;
              step.type = TypeCheckerStep::Type::LookupVariable;
              step.variable_name = node.name;
              step.type_result = type_to_string(result);
              step.description =
                  "lookup '" + node.name + "' -> " + type_to_string(result);
              trace_step(step);
            }
          } else {
            if (trace_) {
              TypeCheckerStep step;
              step.type = TypeCheckerStep::Type::LookupVariableFail;
              step.variable_name = node.name;
              step.description =
                  "lookup '" + node.name + "' -> not found (any)";
              trace_step(step);
            }
          }
          trace_exit(true, "Identifier", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          std::string op_str(token_kind_to_string(node.op));
          trace_enter(true, "BinaryExpr", op_str, expr.location.line,
                      expr.location.column);
          Type left = check_expr(*node.left);
          Type right = check_expr(*node.right);

          switch (node.op) {
          case TokenKind::Plus:
            if (is_string_type(left) || is_string_type(right)) {
              result = Type{StringType{}};
            } else if (is_number_type(left) && is_number_type(right)) {
              result = Type{NumberType{}};
            } else if (is_any_type(left) || is_any_type(right)) {
              result = Type{AnyType{}};
            } else {
              result = Type{AnyType{}};
            }
            break;

          case TokenKind::Minus:
          case TokenKind::Star:
          case TokenKind::Slash:
          case TokenKind::Percent:
          case TokenKind::StarStar:
            if (is_number_type(left) && is_number_type(right)) {
              result = Type{NumberType{}};
            } else if (is_any_type(left) || is_any_type(right)) {
              result = Type{AnyType{}};
            } else {
              result = Type{AnyType{}};
            }
            break;

          case TokenKind::EqualEqual:
          case TokenKind::BangEqual:
          case TokenKind::EqualEqualEqual:
          case TokenKind::BangEqualEqual:
          case TokenKind::Less:
          case TokenKind::LessEqual:
          case TokenKind::Greater:
          case TokenKind::GreaterEqual:
            result = Type{BooleanType{}};
            break;

          case TokenKind::AmpersandAmpersand:
          case TokenKind::PipePipe:
            if (type_to_string(left) == type_to_string(right)) {
              result = left;
            } else {
              result = Type{AnyType{}};
            }
            break;

          default:
            result = Type{AnyType{}};
            break;
          }

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::CheckBinaryOp;
            step.operator_kind = op_str;
            step.type_result = type_to_string(result);
            step.description = type_to_string(left) + " " + op_str + " " +
                               type_to_string(right) + " -> " +
                               type_to_string(result);
            step.source_line = expr.location.line;
            step.source_column = expr.location.column;
            trace_step(step);
          }
          trace_exit(true, "BinaryExpr", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          std::string op_str(token_kind_to_string(node.op));
          trace_enter(true, "UnaryExpr", op_str, expr.location.line,
                      expr.location.column);
          Type operand = check_expr(*node.operand);

          switch (node.op) {
          case TokenKind::Minus:
            if (is_number_type(operand)) {
              result = Type{NumberType{}};
            } else {
              result = Type{AnyType{}};
            }
            break;
          case TokenKind::Bang:
            result = Type{BooleanType{}};
            break;
          case TokenKind::Tilde:
            result = Type{NumberType{}};
            break;
          case TokenKind::TypeOf:
            result = Type{StringType{}};
            break;
          default:
            result = Type{AnyType{}};
            break;
          }

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::CheckUnaryOp;
            step.operator_kind = op_str;
            step.type_result = type_to_string(result);
            step.description = op_str + " " + type_to_string(operand) +
                               " -> " + type_to_string(result);
            step.source_line = expr.location.line;
            step.source_column = expr.location.column;
            trace_step(step);
          }
          trace_exit(true, "UnaryExpr", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
          trace_enter(true, "AssignmentExpr",
                      std::string(token_kind_to_string(node.op)),
                      expr.location.line, expr.location.column);
          Type val_type = check_expr(*node.value);

          if (auto *ident = std::get_if<Identifier>(node.target.get())) {
            auto target_type = env_->lookup(ident->name);
            if (target_type && !is_any_type(*target_type) &&
                !is_any_type(val_type) &&
                type_to_string(*target_type) != type_to_string(val_type)) {
              warn(node.value->location, "Type '" + type_to_string(val_type) +
                                             "' is not assignable to type '" +
                                             type_to_string(*target_type) +
                                             "'.");
            }
          }
          result = val_type;
          trace_exit(true, "AssignmentExpr", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          trace_enter(true, "ConditionalExpr", "", expr.location.line,
                      expr.location.column);
          check_expr(*node.condition);
          Type consequent = check_expr(*node.consequent);
          Type alternate = check_expr(*node.alternate);
          if (type_to_string(consequent) == type_to_string(alternate)) {
            result = consequent;
          } else {
            UnionType u;
            u.members.push_back(std::make_shared<Type>(consequent));
            u.members.push_back(std::make_shared<Type>(alternate));
            result = Type{u};
          }
          trace_exit(true, "ConditionalExpr", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, CallExpr>) {
          trace_enter(true, "CallExpr", "", expr.location.line,
                      expr.location.column);
          Type callee_type = check_expr(*node.callee);
          std::vector<Type> arg_types;
          for (auto &arg : node.arguments) {
            arg_types.push_back(check_expr(*arg));
          }
          if (is_function_type(callee_type)) {
            auto &ft = std::get<FunctionType>(callee_type);

            if (trace_) {
              TypeCheckerStep step;
              step.type = TypeCheckerStep::Type::CheckCallArgs;
              step.description =
                  "check " + std::to_string(arg_types.size()) +
                  " args against " + std::to_string(ft.param_types.size()) +
                  " params";
              trace_step(step);
            }

            if (arg_types.size() != ft.param_types.size()) {
              warn(expr.location, "Expected " +
                                      std::to_string(ft.param_types.size()) +
                                      " arguments, but got " +
                                      std::to_string(arg_types.size()) + ".");
            }
            size_t check_count =
                std::min(arg_types.size(), ft.param_types.size());
            for (size_t i = 0; i < check_count; ++i) {
              if (!is_any_type(*ft.param_types[i]) &&
                  !is_any_type(arg_types[i]) &&
                  type_to_string(*ft.param_types[i]) !=
                      type_to_string(arg_types[i])) {
                warn(expr.location,
                     "Argument of type '" + type_to_string(arg_types[i]) +
                         "' is not assignable to parameter of type '" +
                         type_to_string(*ft.param_types[i]) + "'.");
              }
            }
            result = *ft.return_type;
          }
          trace_exit(true, "CallExpr", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          trace_enter(true, "MemberExpr",
                      node.is_computed ? "[]" : "." + node.property,
                      expr.location.line, expr.location.column);
          check_expr(*node.object);
          trace_exit(true, "MemberExpr", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          trace_enter(true, "ArrayLiteral", "", expr.location.line,
                      expr.location.column);
          for (auto &elem : node.elements) {
            check_expr(*elem);
          }
          trace_exit(true, "ArrayLiteral", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          trace_enter(true, "ObjectLiteral", "", expr.location.line,
                      expr.location.column);
          for (auto &prop : node.properties) {
            check_expr(*prop.value);
          }
          trace_exit(true, "ObjectLiteral", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, ArrowFunction>) {
          trace_enter(true, "ArrowFunction", "", expr.location.line,
                      expr.location.column);
          Type fn_type = build_function_type(node.params, node.return_type);

          TypeEnvironment arrow_env(env_);
          TypeEnvironment *saved_env = env_;
          std::string saved_label = current_scope_label_;
          env_ = &arrow_env;
          current_scope_label_ = "arrow";

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::EnterFunction;
            step.description = "enter arrow function";
            step.scope_label = "arrow";
            trace_step(step);
          }

          auto &ft = std::get<FunctionType>(fn_type);
          for (size_t i = 0; i < node.params.size(); ++i) {
            env_->define(node.params[i].name, *ft.param_types[i]);
            if (trace_) {
              TypeCheckerStep step;
              step.type = TypeCheckerStep::Type::DefineParam;
              step.variable_name = node.params[i].name;
              step.type_result = type_to_string(*ft.param_types[i]);
              step.description = "param " + node.params[i].name + ": " +
                                 type_to_string(*ft.param_types[i]);
              capture_scope_snapshot(step);
              trace_step(step);
            }
          }

          Type return_type = *ft.return_type;
          Type *saved_return_type = current_return_type_;
          current_return_type_ = &return_type;

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::SetReturnType;
            step.type_result = type_to_string(return_type);
            step.description =
                "expected return type: " + type_to_string(return_type);
            trace_step(step);
          }

          std::visit(
              [&](auto &body) {
                using B = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<B, ExprPtr>) {
                  Type body_type = check_expr(*body);
                  if (!is_any_type(return_type) && !is_any_type(body_type) &&
                      type_to_string(return_type) !=
                          type_to_string(body_type)) {
                    warn(expr.location, "Type '" + type_to_string(body_type) +
                                            "' is not assignable to type '" +
                                            type_to_string(return_type) + "'.");
                  }
                } else {
                  check_stmt(*body);
                }
              },
              node.body);

          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::ExitFunction;
            step.description = "exit arrow function";
            step.scope_label = "arrow";
            capture_scope_snapshot(step);
            trace_step(step);
          }

          current_return_type_ = saved_return_type;
          env_ = saved_env;
          current_scope_label_ = saved_label;
          result = fn_type;
          trace_exit(true, "ArrowFunction", type_to_string(result),
                     expr.location.line, expr.location.column);

        } else if constexpr (std::is_same_v<T, TemplateLiteral>) {
          trace_enter(true, "TemplateLiteral", "", expr.location.line,
                      expr.location.column);
          for (auto &e : node.expressions) {
            check_expr(*e);
          }
          result = Type{StringType{}};
          if (trace_) {
            TypeCheckerStep step;
            step.type = TypeCheckerStep::Type::InferFromLiteral;
            step.type_result = "string";
            step.description = "template literal -> string";
            trace_step(step);
          }
          trace_exit(true, "TemplateLiteral", "string", expr.location.line,
                     expr.location.column);
        }
      },
      static_cast<Expr::variant &>(expr));

  expr.resolved_type = result;
  return result;
}

Type TypeChecker::build_function_type(const std::vector<Parameter> &params,
                                      const TypeAnnotationPtr &return_type) {
  FunctionType ft;
  for (const auto &param : params) {
    if (param.type_annotation) {
      ft.param_types.push_back(
          std::make_shared<Type>(resolve_annotation(*param.type_annotation)));
    } else {
      ft.param_types.push_back(std::make_shared<Type>(AnyType{}));
    }
  }
  if (return_type) {
    ft.return_type = std::make_shared<Type>(resolve_annotation(*return_type));
  } else {
    ft.return_type = std::make_shared<Type>(AnyType{});
  }
  return Type{ft};
}

} // namespace yatsi
