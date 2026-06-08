#pragma once

#include <string>
#include <vector>

#include "parser/ast.h"
#include "typechecker/type_env.h"
#include "typechecker/types.h"

namespace yatsi {

class TypeChecker {
public:
  void check(Program &program);

  bool has_warnings() const { return !warnings_.empty(); }
  const std::vector<std::string> &warnings() const { return warnings_; }

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
};

} // namespace yatsi