#include "typechecker/checker.h"
#include "typechecker/types.h"

#include <sstream>

namespace yatsi {

// --- Public entry point ---

void TypeChecker::check(Program &program) {
  TypeEnvironment global;
  env_ = &global;

  for (auto &stmt : program.body) {
    check_stmt(*stmt);
  }

  env_ = nullptr;
}

// --- Annotation resolution ---

Type TypeChecker::resolve_annotation(const TypeAnnotation &annotation) {
  if (annotation.name == "number")
    return Type{NumberType{}};
  if (annotation.name == "string")
    return Type{StringType{}};
  if (annotation.name == "boolean")
    return Type{BooleanType{}};
  if (annotation.name == "void")
    return Type{VoidType{}};
  if (annotation.name == "null")
    return Type{NullType{}};
  if (annotation.name == "undefined")
    return Type{UndefinedType{}};
  // Unknown type annotation — treat as any
  return Type{AnyType{}};
}

// --- Diagnostics ---

void TypeChecker::warn(const SourceLocation &loc, const std::string &message) {
  std::ostringstream oss;
  oss << loc.file << ":" << loc.line << ":" << loc.column
      << ": warning: " << message;
  warnings_.push_back(oss.str());
}

// --- Statement checking ---

void TypeChecker::check_stmt(Stmt &stmt) {
  std::visit(
      [&](auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
          check_expr(*node.expression);

        } else if constexpr (std::is_same_v<T, VarDeclaration>) {
          Type declared_type{AnyType{}};
          if (node.type_annotation) {
            declared_type = resolve_annotation(*node.type_annotation);
          }

          if (node.initializer) {
            Type init_type = check_expr(*node.initializer);

            if (node.type_annotation) {
              // Check: initializer type matches annotation
              if (!is_any_type(declared_type) && !is_any_type(init_type) &&
                  type_to_string(declared_type) != type_to_string(init_type)) {
                warn(node.initializer->location,
                     "Type '" + type_to_string(init_type) +
                         "' is not assignable to type '" +
                         type_to_string(declared_type) + "'.");
              }
            } else {
              // No annotation — infer type from initializer
              declared_type = init_type;
            }
          }

          env_->define(node.name, declared_type);

        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          TypeEnvironment block_env(env_);
          TypeEnvironment *saved = env_;
          env_ = &block_env;
          for (auto &s : node.statements) {
            check_stmt(*s);
          }
          env_ = saved;

        } else if constexpr (std::is_same_v<T, IfStmt>) {
          check_expr(*node.condition);
          check_stmt(*node.consequent);
          if (node.alternate) {
            check_stmt(*node.alternate);
          }

        } else if constexpr (std::is_same_v<T, WhileStmt>) {
          check_expr(*node.condition);
          check_stmt(*node.body);

        } else if constexpr (std::is_same_v<T, ForStmt>) {
          // For loops get their own scope (for the init variable)
          TypeEnvironment for_env(env_);
          TypeEnvironment *saved = env_;
          env_ = &for_env;
          if (node.init)
            check_stmt(*node.init);
          if (node.condition)
            check_expr(*node.condition);
          if (node.update)
            check_expr(*node.update);
          check_stmt(*node.body);
          env_ = saved;

        } else if constexpr (std::is_same_v<T, FunctionDecl>) {
          // Build FunctionType from annotations
          Type fn_type = build_function_type(node.params, node.return_type);
          env_->define(node.name, fn_type);

          // Push function scope
          TypeEnvironment func_env(env_);
          TypeEnvironment *saved_env = env_;
          env_ = &func_env;

          // Define parameters in function scope
          auto &ft = std::get<FunctionType>(fn_type);
          for (size_t i = 0; i < node.params.size(); ++i) {
            env_->define(node.params[i].name, *ft.param_types[i]);
          }

          // Track return type for ReturnStmt checking
          Type return_type = *ft.return_type;
          Type *saved_return_type = current_return_type_;
          current_return_type_ = &return_type;

          // Check body
          check_stmt(*node.body);

          current_return_type_ = saved_return_type;
          env_ = saved_env;

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          if (node.value) {
            Type val_type = check_expr(*node.value);
            if (current_return_type_ && !is_any_type(*current_return_type_) &&
                !is_any_type(val_type) &&
                type_to_string(*current_return_type_) !=
                    type_to_string(val_type)) {
              warn(node.value->location,
                   "Type '" + type_to_string(val_type) +
                       "' is not assignable to type '" +
                       type_to_string(*current_return_type_) + "'.");
            }
          }

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
          result = Type{NumberType{}};

        } else if constexpr (std::is_same_v<T, StringLiteral>) {
          result = Type{StringType{}};

        } else if constexpr (std::is_same_v<T, BooleanLiteral>) {
          result = Type{BooleanType{}};

        } else if constexpr (std::is_same_v<T, NullLiteral>) {
          result = Type{NullType{}};

        } else if constexpr (std::is_same_v<T, UndefinedLiteral>) {
          result = Type{UndefinedType{}};

        } else if constexpr (std::is_same_v<T, Identifier>) {
          auto found = env_->lookup(node.name);
          if (found) {
            result = *found;
          }

        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          Type left = check_expr(*node.left);
          Type right = check_expr(*node.right);

          switch (node.op) {
          // A number plus a number is an umber, a string plus any other type
          // will become a string
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

            // Other types of numeric arithmetic will always produce numbers,
            // fall back to `any`
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

            // Comparison always returns a boolean
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

            // Logical operators will always returnt he type of one of their
            // operands
          case TokenKind::AmpersandAmpersand:
          case TokenKind::PipePipe:
            // Simplified: if both same type, that type; otherwise any
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
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
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
        } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
          Type val_type = check_expr(*node.value);

          // Check target type if it's an identifier
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
        } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          check_expr(*node.condition);
          Type consequent = check_expr(*node.consequent);
          Type alternate = check_expr(*node.alternate);
          // If both branches same type, use that; otherwise union
          if (type_to_string(consequent) == type_to_string(alternate)) {
            result = consequent;
          } else {
            UnionType u;
            u.members.push_back(std::make_shared<Type>(consequent));
            u.members.push_back(std::make_shared<Type>(alternate));
            result = Type{u};
          }
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          Type callee_type = check_expr(*node.callee);
          // Check arguments
          std::vector<Type> arg_types;
          for (auto &arg : node.arguments) {
            check_expr(*arg);
            arg_types.push_back(check_expr(*arg));
          }
          if (is_function_type(callee_type)) {
            auto &ft = std::get<FunctionType>(callee_type);
            // Check argument count
            if (arg_types.size() != ft.param_types.size()) {
              warn(expr.location, "Expected " +
                                      std::to_string(ft.param_types.size()) +
                                      " arguments, but got " +
                                      std::to_string(arg_types.size()) + ".");
            }
            // Check argument types
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
          // else: callee is AnyType or unknown, result stays AnyType
        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          check_expr(*node.object);
          // TODO: fix types when we build member access code
        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          for (auto &elem : node.elements) {
            check_expr(*elem);
          }
        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          for (auto &prop : node.properties) {
            check_expr(*prop.value);
          }
        } else if constexpr (std::is_same_v<T, ArrowFunction>) {
          Type fn_type = build_function_type(node.params, node.return_type);

          // Push function scope and define params
          TypeEnvironment arrow_env(env_);
          TypeEnvironment *saved_env = env_;
          env_ = &arrow_env;

          auto &ft = std::get<FunctionType>(fn_type);
          for (size_t i = 0; i < node.params.size(); ++i) {
            env_->define(node.params[i].name, *ft.param_types[i]);
          }

          // Track return type
          Type return_type = *ft.return_type;
          Type *saved_return_type = current_return_type_;
          current_return_type_ = &return_type;

          // Check body
          std::visit(
              [&](auto &body) {
                using B = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<B, ExprPtr>) {
                  Type body_type = check_expr(*body);
                  // Expression body is an implicit return
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

          current_return_type_ = saved_return_type;
          env_ = saved_env;
          result = fn_type;
        } else if constexpr (std::is_same_v<T, TemplateLiteral>) {
          for (auto &e : node.expressions) {
            check_expr(*e);
          }
          result = Type{StringType{}};
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