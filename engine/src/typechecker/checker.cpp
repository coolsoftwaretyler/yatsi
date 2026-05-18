#include "typechecker/checker.h"

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
          // Stub: will be fully handled in Chunk 5
          env_->define(node.name, Type{AnyType{}});

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          if (node.value) {
            check_expr(*node.value);
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
          auto type = env_->lookup(node.name);
          if (type) {
            result = *type;
          }
          // else: unknown variable, stays AnyType

        } else if constexpr (std::is_same_v<T, TemplateLiteral>) {
          // Template literals always produce strings
          for (auto &e : node.expressions) {
            check_expr(*e);
          }
          result = Type{StringType{}};
        }
        // Other expression types (BinaryExpr, UnaryExpr, etc.) handled in Chunk
        // 4
      },
      static_cast<Expr::variant &>(expr));

  expr.resolved_type = result;
  return result;
}

} // namespace yatsi 