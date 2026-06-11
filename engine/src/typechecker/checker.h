#pragma once

#include <string>
#include <utility>
#include <vector>

#include "parser/ast.h"
#include "typechecker/type_env.h"
#include "typechecker/types.h"

namespace yatsi {

struct TypeCheckerStep {
  enum class Type : uint8_t {
    // Scope operations
    EnterScope,
    ExitScope,

    // Variable operations
    DefineVariable,
    LookupVariable,
    LookupVariableFail,

    // Type resolution
    ResolveAnnotation,
    InferFromLiteral,
    InferFromInitializer,

    // Expression/statement traversal
    EnterExpr,
    ExitExpr,
    EnterStmt,
    ExitStmt,

    // Operator type rules
    CheckBinaryOp,
    CheckUnaryOp,

    // Function checking
    EnterFunction,
    ExitFunction,
    DefineParam,
    SetReturnType,
    CheckReturnValue,
    CheckCallArgs,

    // Diagnostics
    Warning,
  };

  Type type;
  size_t depth = 0;
  std::string node_type;
  std::string description;
  std::string variable_name;
  std::string type_result;
  std::string expected_type;
  std::string actual_type;
  std::string operator_kind;
  int source_line = -1;
  int source_column = -1;

  // Scope snapshot
  std::vector<std::pair<std::string, std::string>> scope_bindings;
  std::string scope_label;
};

class TypeChecker {
public:
  void check(Program &program);

  bool has_warnings() const { return !warnings_.empty(); }
  const std::vector<std::string> &warnings() const { return warnings_; }

  void enable_tracing(std::vector<TypeCheckerStep> &trace);

private:
  TypeEnvironment *env_ = nullptr;

  void check_stmt(Stmt &stmt);
  Type check_expr(Expr &expr);

  // Resolve a type annotation string ("number", "string", etc.) to a Type
  Type resolve_annotation(const TypeAnnotation &annotation);

  void warn(const SourceLocation &loc, const std::string &message);

  Type build_function_type(const std::vector<Parameter> &params,
                           const TypeAnnotationPtr &return_type);

  Type *current_return_type_ = nullptr;

  std::vector<std::string> warnings_;

  // Tracing infrastructure
  std::vector<TypeCheckerStep> *trace_ = nullptr;
  size_t trace_depth_ = 0;
  void trace_step(TypeCheckerStep step);
  void capture_scope_snapshot(TypeCheckerStep &step);
  void trace_enter(bool is_expr, const std::string &node_type,
                   const std::string &detail, int line, int col);
  void trace_exit(bool is_expr, const std::string &node_type,
                  const std::string &type_result, int line, int col);
  std::string current_scope_label_;
};

} // namespace yatsi
