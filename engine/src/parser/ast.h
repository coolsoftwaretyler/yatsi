#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "common/source_location.h"
#include "lexer/token.h"

namespace yatsi {

// Forward declarations
struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// --- Supporting types ---

struct TypeAnnotation {
  std::string name; // "number", "string", etc.
  SourceLocation location;
};
using TypeAnnotationPtr = std::unique_ptr<TypeAnnotation>;

struct Parameter {
  std::string name;
  TypeAnnotationPtr type_annotation; // nullptr if absent
  SourceLocation location;
};

struct ObjectProperty {
  ExprPtr key;   // Identifier or StringLiteral
  ExprPtr value;
};

// --- Expression nodes ---

struct NumberLiteral {
  double value;
};

struct StringLiteral {
  std::string value;
};

struct BooleanLiteral {
  bool value;
};

struct NullLiteral {};

struct UndefinedLiteral {};

struct Identifier {
  std::string name;
};

struct BinaryExpr {
  TokenKind op;
  ExprPtr left;
  ExprPtr right;
};

struct UnaryExpr {
  TokenKind op;
  ExprPtr operand;
  bool prefix;
};

struct AssignmentExpr {
  TokenKind op; // Equal, PlusEqual, etc.
  ExprPtr target;
  ExprPtr value;
};

struct CallExpr {
  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};

struct MemberExpr {
  ExprPtr object;
  std::string property;  // used when !is_computed
  ExprPtr computed;       // used when is_computed (bracket notation)
  bool is_computed;
};

struct ArrayLiteral {
  std::vector<ExprPtr> elements;
};

struct ObjectLiteral {
  std::vector<ObjectProperty> properties;
};

struct ArrowFunction {
  std::vector<Parameter> params;
  TypeAnnotationPtr return_type;
  std::variant<ExprPtr, StmtPtr> body; // expression body or block
};

struct ConditionalExpr {
  ExprPtr condition;
  ExprPtr consequent;
  ExprPtr alternate;
};

struct TemplateLiteral {
  std::vector<std::string> quasis;
  std::vector<ExprPtr> expressions;
};

// --- Statement nodes ---

struct ExpressionStmt {
  ExprPtr expression;
};

struct VarDeclaration {
  TokenKind kind; // Let, Const, Var
  std::string name;
  TypeAnnotationPtr type_annotation;
  ExprPtr initializer; // nullptr if absent
};

struct BlockStmt {
  std::vector<StmtPtr> statements;
};

struct IfStmt {
  ExprPtr condition;
  StmtPtr consequent;
  StmtPtr alternate; // nullptr if no else
};

struct WhileStmt {
  ExprPtr condition;
  StmtPtr body;
};

struct ForStmt {
  StmtPtr init;    // VarDeclaration or ExpressionStmt
  ExprPtr condition;
  ExprPtr update;
  StmtPtr body;
};

struct FunctionDecl {
  std::string name;
  std::vector<Parameter> params;
  TypeAnnotationPtr return_type;
  StmtPtr body; // BlockStmt
};

struct ReturnStmt {
  ExprPtr value; // nullptr for bare return
};

// --- Expr / Stmt variant wrappers ---

struct Expr : std::variant<NumberLiteral, StringLiteral, BooleanLiteral,
                           NullLiteral, UndefinedLiteral, Identifier,
                           BinaryExpr, UnaryExpr, AssignmentExpr, CallExpr,
                           MemberExpr, ArrayLiteral, ObjectLiteral,
                           ArrowFunction, ConditionalExpr, TemplateLiteral> {
  using variant::variant;
  SourceLocation location;
};

struct Stmt : std::variant<ExpressionStmt, VarDeclaration, BlockStmt, IfStmt,
                           WhileStmt, ForStmt, FunctionDecl, ReturnStmt> {
  using variant::variant;
  SourceLocation location;
};

// --- Root node ---

struct Program {
  std::vector<StmtPtr> body;
};

} // namespace yatsi
