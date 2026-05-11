#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/source_location.h"
#include "lexer/token.h"
#include "parser/ast.h"

namespace yatsi {

struct ParserStep {
  enum class Type : uint8_t {
    EnterRule,
    ExitRule,
    ConsumeToken,
    TryMatch,
    ProduceNode,
    Error,
    Synchronize
  };
  Type type;
  size_t token_pos;
  size_t depth = 0;
  std::string rule;
  std::string description;
};

class Parser {
public:
  Parser(std::vector<Token> tokens, std::string filename);
  Program parse();
  bool has_errors() const;
  const std::vector<std::string>& errors() const;
  void enable_tracing(std::vector<ParserStep>& trace);

private:
  std::vector<Token> tokens_;
  std::string file_name_;
  size_t pos_ = 0;
  std::vector<std::string> errors_;
  std::vector<ParserStep>* trace_ = nullptr;
  size_t trace_depth_ = 0;
  void trace_step(ParserStep step);
  const Token &current() const;
  const Token &peek() const;
  Token advance();
  bool check(TokenKind kind) const;
  bool match(TokenKind kind);
  Token expect(TokenKind expected, const std::string& message);
  bool at_end() const;
  void error(const std::string &message);
  ExprPtr make_expr(Expr node, const SourceLocation& loc);
  StmtPtr make_stmt(Stmt node, const SourceLocation& loc);
  void synchronize();
  ExprPtr parse_expression();
  ExprPtr parse_assignment();
  ExprPtr parse_conditional();
  ExprPtr parse_binary_expr(int min_prec);
  ExprPtr parse_unary();
  ExprPtr parse_postfix();
  ExprPtr parse_call_or_member();
  ExprPtr parse_primary();
  StmtPtr parse_statement();
  StmtPtr parse_expression_statement();
  StmtPtr parse_var_declaration();
  StmtPtr parse_block();
  StmtPtr parse_if_statement();
  StmtPtr parse_while_statement();
  StmtPtr parse_for_statement();
  StmtPtr parse_return_statement();
  StmtPtr parse_function_declaration();
  TypeAnnotationPtr parse_type_annotation();
  std::vector<Parameter> parse_parameter_list();
  std::vector<ExprPtr> parse_arguments();
  ExprPtr parse_arrow_function();
  ExprPtr parse_array_literal();
  ExprPtr parse_object_literal();
  ExprPtr parse_template_literal();
  bool is_arrow_function();
};
} // namespace yatsi
