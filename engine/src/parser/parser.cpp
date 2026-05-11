#include <string>

#include "common/source_location.h"
#include "lexer/token.h"
#include "parser/ast.h"
#include "parser/parser.h"

namespace yatsi {

// --- Tracing infrastructure ---

void Parser::enable_tracing(std::vector<ParserStep>& trace) {
  trace_ = &trace;
}

void Parser::trace_step(ParserStep step) {
  if (trace_) {
    step.depth = trace_depth_;
    trace_->push_back(std::move(step));
  }
}

// Helper: brief token description e.g. "Let 'let'" or "Number '3'"
static std::string tok_desc(const Token& tok) {
  return std::string(token_kind_to_string(tok.kind)) + " '" + tok.lexeme + "'";
}

// RAII helper — emits EnterRule on construction, ExitRule on destruction
struct TraceRule {
  std::vector<ParserStep>* trace_;
  size_t* depth_;
  std::string rule_;
  std::string result_;
  size_t token_pos_;

  TraceRule(std::vector<ParserStep>* t, size_t* depth, const std::string& rule,
            size_t pos, const std::string& detail)
      : trace_(t), depth_(depth), rule_(rule), result_("pass through"), token_pos_(pos) {
    if (trace_) {
      trace_->push_back(ParserStep{ParserStep::Type::EnterRule, pos, *depth_, rule, detail});
      (*depth_)++;
    }
  }

  ~TraceRule() {
    if (trace_) {
      (*depth_)--;
      trace_->push_back(ParserStep{ParserStep::Type::ExitRule, token_pos_, *depth_, rule_, result_});
    }
  }

  void set_result(const std::string& desc) { result_ = desc; }
  void update_pos(size_t pos) { token_pos_ = pos; }
};

static int binary_precedence(TokenKind kind) {
  switch (kind) {
  case TokenKind::PipePipe:
  case TokenKind::QuestionQuestion:
    return 1;
  case TokenKind::AmpersandAmpersand:
    return 2;
  case TokenKind::Pipe:
    return 3;
  case TokenKind::Caret:
    return 4;
  case TokenKind::Ampersand:
    return 5;
  case TokenKind::EqualEqual:
  case TokenKind::BangEqual:
  case TokenKind::EqualEqualEqual:
  case TokenKind::BangEqualEqual:
    return 6;
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual:
    return 7;
  case TokenKind::LessLess:
  case TokenKind::GreaterGreater:
  case TokenKind::GreaterGreaterGreater:
    return 8;
  case TokenKind::Plus:
  case TokenKind::Minus:
    return 9;
  case TokenKind::Star:
  case TokenKind::Slash:
  case TokenKind::Percent:
    return 10;
  case TokenKind::StarStar:
    return 11;
  default:
    return -1;
  }
}

static bool is_right_associative(TokenKind kind) {
  return kind == TokenKind::StarStar;
}
Parser::Parser(std::vector<Token> tokens, std::string filename)
    : tokens_(std::move(tokens)), file_name_(std::move(filename)) {}

const Token &Parser::current() const { return tokens_[pos_]; }

bool Parser::at_end() const { return current().kind == TokenKind::EndOfFile; }

const Token &Parser::peek() const {
  if (pos_ + 1 < tokens_.size())
    return tokens_[pos_ + 1];
  return tokens_.back();
}

Token Parser::advance() {
  Token tok = tokens_[pos_];
  if (!at_end()) {
    pos_++;
  }
  return tok;
}

// Traced versions of token consumption (called by parse functions)
// The raw advance/match/expect remain fast; tracing is done at call sites.

bool Parser::check(TokenKind kind) const { return tokens_[pos_].kind == kind; }

bool Parser::match(TokenKind kind) {
  if (check(kind)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "",
          "match " + std::string(token_kind_to_string(kind)) + " -> consumed"});
    }
    advance();
    return true;
  }
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::TryMatch, pos_, 0, "",
        "match " + std::string(token_kind_to_string(kind)) + " -> skip (saw " +
        tok_desc(current()) + ")"});
  }
  return false;
}

Token Parser::expect(TokenKind kind, const std::string &message) {
  if (check(kind)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "",
          "expect " + std::string(token_kind_to_string(kind)) + " -> '" +
          current().lexeme + "'"});
    }
    return advance();
  }
  error(message);
  return current();
}

void Parser::error(const std::string &message) {
  const auto &tok = current();
  std::string msg = file_name_ + ":" + std::to_string(tok.location.line) + ":" +
                    std::to_string(tok.location.column) + ": " + message;
  errors_.push_back(msg);
  trace_step(ParserStep{ParserStep::Type::Error, pos_, 0, "", message});
}

void Parser::synchronize() {
  size_t start_pos = pos_;
  advance();
  while (!at_end()) {
    if (tokens_[pos_ - 1].kind == TokenKind::Semicolon) {
      trace_step(ParserStep{ParserStep::Type::Synchronize, pos_, 0, "",
          "skipped to " + tok_desc(current()) + " at pos " + std::to_string(pos_)});
      return;
    }

    switch (current().kind) {
    case TokenKind::Let:
    case TokenKind::Const:
    case TokenKind::Var:
    case TokenKind::Function:
    case TokenKind::If:
    case TokenKind::While:
    case TokenKind::For:
    case TokenKind::Return:
      trace_step(ParserStep{ParserStep::Type::Synchronize, pos_, 0, "",
          "skipped to " + tok_desc(current()) + " at pos " + std::to_string(pos_)});
      return;
    default:
      advance();
    }
  }
  trace_step(ParserStep{ParserStep::Type::Synchronize, pos_, 0, "",
      "skipped to end from pos " + std::to_string(start_pos)});
}

bool Parser::has_errors() const { return !errors_.empty(); }

const std::vector<std::string> &Parser::errors() const { return errors_; }

static std::string expr_type_desc(const Expr& e) {
  return std::visit([](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, NumberLiteral>) {
      // Format integer values without decimal point
      if (v.value == static_cast<int64_t>(v.value))
        return "NumberLiteral(" + std::to_string(static_cast<int64_t>(v.value)) + ")";
      return "NumberLiteral(" + std::to_string(v.value) + ")";
    }
    else if constexpr (std::is_same_v<T, StringLiteral>) return "StringLiteral(\"" + v.value + "\")";
    else if constexpr (std::is_same_v<T, BooleanLiteral>) return v.value ? "BooleanLiteral(true)" : "BooleanLiteral(false)";
    else if constexpr (std::is_same_v<T, NullLiteral>) return "NullLiteral";
    else if constexpr (std::is_same_v<T, UndefinedLiteral>) return "UndefinedLiteral";
    else if constexpr (std::is_same_v<T, Identifier>) return "Identifier(" + v.name + ")";
    else if constexpr (std::is_same_v<T, BinaryExpr>) return "BinaryExpr(" + std::string(token_kind_to_string(v.op)) + ")";
    else if constexpr (std::is_same_v<T, UnaryExpr>) return "UnaryExpr(" + std::string(token_kind_to_string(v.op)) + ")";
    else if constexpr (std::is_same_v<T, AssignmentExpr>) return "AssignmentExpr(" + std::string(token_kind_to_string(v.op)) + ")";
    else if constexpr (std::is_same_v<T, CallExpr>) return "CallExpr";
    else if constexpr (std::is_same_v<T, MemberExpr>) return v.is_computed ? "MemberExpr([])" : "MemberExpr(." + v.property + ")";
    else if constexpr (std::is_same_v<T, ArrayLiteral>) return "ArrayLiteral";
    else if constexpr (std::is_same_v<T, ObjectLiteral>) return "ObjectLiteral";
    else if constexpr (std::is_same_v<T, ArrowFunction>) return "ArrowFunction";
    else if constexpr (std::is_same_v<T, ConditionalExpr>) return "ConditionalExpr";
    else if constexpr (std::is_same_v<T, TemplateLiteral>) return "TemplateLiteral";
    else return "?";
  }, static_cast<const Expr::variant&>(e));
}

static std::string expr_type_name(const Expr& e) {
  return std::visit([](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, NumberLiteral>) return "NumberLiteral";
    else if constexpr (std::is_same_v<T, StringLiteral>) return "StringLiteral";
    else if constexpr (std::is_same_v<T, BooleanLiteral>) return "BooleanLiteral";
    else if constexpr (std::is_same_v<T, NullLiteral>) return "NullLiteral";
    else if constexpr (std::is_same_v<T, UndefinedLiteral>) return "UndefinedLiteral";
    else if constexpr (std::is_same_v<T, Identifier>) return "Identifier";
    else if constexpr (std::is_same_v<T, BinaryExpr>) return "BinaryExpr";
    else if constexpr (std::is_same_v<T, UnaryExpr>) return "UnaryExpr";
    else if constexpr (std::is_same_v<T, AssignmentExpr>) return "AssignmentExpr";
    else if constexpr (std::is_same_v<T, CallExpr>) return "CallExpr";
    else if constexpr (std::is_same_v<T, MemberExpr>) return "MemberExpr";
    else if constexpr (std::is_same_v<T, ArrayLiteral>) return "ArrayLiteral";
    else if constexpr (std::is_same_v<T, ObjectLiteral>) return "ObjectLiteral";
    else if constexpr (std::is_same_v<T, ArrowFunction>) return "ArrowFunction";
    else if constexpr (std::is_same_v<T, ConditionalExpr>) return "ConditionalExpr";
    else if constexpr (std::is_same_v<T, TemplateLiteral>) return "TemplateLiteral";
    else return "?";
  }, static_cast<const Expr::variant&>(e));
}

static std::string stmt_type_desc(const Stmt& s) {
  return std::visit([](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, ExpressionStmt>) return "ExpressionStmt";
    else if constexpr (std::is_same_v<T, VarDeclaration>) {
      std::string kind_str;
      switch (v.kind) {
        case TokenKind::Let: kind_str = "Let"; break;
        case TokenKind::Const: kind_str = "Const"; break;
        case TokenKind::Var: kind_str = "Var"; break;
        default: kind_str = "?"; break;
      }
      return "VarDeclaration(" + kind_str + " " + v.name + ")";
    }
    else if constexpr (std::is_same_v<T, BlockStmt>) return "BlockStmt";
    else if constexpr (std::is_same_v<T, IfStmt>) return "IfStmt";
    else if constexpr (std::is_same_v<T, WhileStmt>) return "WhileStmt";
    else if constexpr (std::is_same_v<T, ForStmt>) return "ForStmt";
    else if constexpr (std::is_same_v<T, FunctionDecl>) return "FunctionDecl(" + v.name + ")";
    else if constexpr (std::is_same_v<T, ReturnStmt>) return "ReturnStmt";
    else return "?";
  }, static_cast<const Stmt::variant&>(s));
}

static std::string stmt_type_name(const Stmt& s) {
  return std::visit([](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, ExpressionStmt>) return "ExpressionStmt";
    else if constexpr (std::is_same_v<T, VarDeclaration>) return "VarDeclaration";
    else if constexpr (std::is_same_v<T, BlockStmt>) return "BlockStmt";
    else if constexpr (std::is_same_v<T, IfStmt>) return "IfStmt";
    else if constexpr (std::is_same_v<T, WhileStmt>) return "WhileStmt";
    else if constexpr (std::is_same_v<T, ForStmt>) return "ForStmt";
    else if constexpr (std::is_same_v<T, FunctionDecl>) return "FunctionDecl";
    else if constexpr (std::is_same_v<T, ReturnStmt>) return "ReturnStmt";
    else return "?";
  }, static_cast<const Stmt::variant&>(s));
}

ExprPtr Parser::make_expr(Expr node, const SourceLocation &loc) {
  auto ptr = std::make_unique<Expr>(std::move(node));
  ptr->location = loc;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ProduceNode, pos_, 0, "",
        expr_type_desc(*ptr)});
  }
  return ptr;
}

StmtPtr Parser::make_stmt(Stmt node, const SourceLocation &loc) {
  auto ptr = std::make_unique<Stmt>(std::move(node));
  ptr->location = loc;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ProduceNode, pos_, 0, "",
        stmt_type_desc(*ptr)});
  }
  return ptr;
}
// --- Parse entry point ---

Program Parser::parse() {
  Program program;
  while (!at_end()) {
    auto stmt = parse_statement();
    if (stmt) {
      program.body.push_back(std::move(stmt));
    }
  }
  return program;
}

// --- Statements ---

StmtPtr Parser::parse_statement() {
  TraceRule tr(trace_, &trace_depth_, "parse_statement", pos_, "at " + tok_desc(current()));
  StmtPtr result;
  if (check(TokenKind::Let) || check(TokenKind::Const) ||
      check(TokenKind::Var)) {
    result = parse_var_declaration();
  } else if (check(TokenKind::LeftBrace)) {
    result = parse_block();
  } else if (check(TokenKind::If)) {
    result = parse_if_statement();
  } else if (check(TokenKind::While)) {
    result = parse_while_statement();
  } else if (check(TokenKind::For)) {
    result = parse_for_statement();
  } else if (check(TokenKind::Return)) {
    result = parse_return_statement();
  } else if (check(TokenKind::Function)) {
    result = parse_function_declaration();
  } else if (check(TokenKind::Break)) {
    auto loc = current().location;
    advance();
    expect(TokenKind::Semicolon, "Expected ';' after break");
    return make_stmt(BreakStmt{}, loc);
  } else if (check(TokenKind::Continue)) {
    auto loc = current().location;
    advance();
    expect(TokenKind::Semicolon, "Expected ';' after continue");
    return make_stmt(ContinueStmt{}, loc);
  }
  else {
    result = parse_expression_statement();
  }
  if (result) {
    tr.set_result("-> " + std::string(stmt_type_name(*result)));
  }
  tr.update_pos(pos_);
  return result;
}

StmtPtr Parser::parse_expression_statement() {
  TraceRule tr(trace_, &trace_depth_, "parse_expression_statement", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  auto expr = parse_expression();
  if (!expr) return nullptr;
  expect(TokenKind::Semicolon, "Expected ';' after expression");
  auto result = make_stmt(ExpressionStmt{std::move(expr)}, loc);
  tr.set_result("-> ExpressionStmt");
  tr.update_pos(pos_);
  return result;
}

// --- Expression chain (pass-throughs for now) ---

ExprPtr Parser::parse_expression() {
  TraceRule tr(trace_, &trace_depth_, "parse_expression", pos_, "at " + tok_desc(current()));
  auto result = parse_assignment();
  if (result) {
    tr.set_result("-> " + std::string(expr_type_name(*result)));
  }
  tr.update_pos(pos_);
  return result;
}

ExprPtr Parser::parse_assignment() {
  TraceRule tr(trace_, &trace_depth_, "parse_assignment", pos_, "at " + tok_desc(current()));
  auto expr = parse_conditional();
  if (!expr) { tr.update_pos(pos_); return nullptr; }

  if (check(TokenKind::Equal) || check(TokenKind::PlusEqual) ||
      check(TokenKind::MinusEqual) || check(TokenKind::StarEqual) ||
      check(TokenKind::SlashEqual) || check(TokenKind::PercentEqual)) {
    auto loc = current().location;
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_assignment",
          "consume " + tok_desc(current())});
    }
    Token op = advance();
    auto value = parse_assignment(); // right-recursive for a = b = c
    if (!value) { tr.update_pos(pos_); return nullptr; }
    auto result = make_expr(
        AssignmentExpr{op.kind, std::move(expr), std::move(value)}, loc);
    tr.set_result("-> AssignmentExpr");
    tr.update_pos(pos_);
    return result;
  }

  tr.update_pos(pos_);
  return expr;
}

ExprPtr Parser::parse_conditional() {
  TraceRule tr(trace_, &trace_depth_, "parse_conditional", pos_, "at " + tok_desc(current()));
  auto expr = parse_binary_expr(0);
  if (!expr) { tr.update_pos(pos_); return nullptr; }

  if (match(TokenKind::Question)) {
    auto loc = current().location;
    auto consequent = parse_expression();
    expect(TokenKind::Colon, "Expected ':' in ternary expression");
    auto alternate = parse_expression();
    auto result = make_expr(
        ConditionalExpr{std::move(expr), std::move(consequent),
                        std::move(alternate)},
        loc);
    tr.set_result("-> ConditionalExpr");
    tr.update_pos(pos_);
    return result;
  }

  tr.update_pos(pos_);
  return expr;
}

ExprPtr Parser::parse_binary_expr(int min_prec) {
  TraceRule tr(trace_, &trace_depth_, "parse_binary_expr", pos_,
      "min_prec=" + std::to_string(min_prec) + ", at " + tok_desc(current()));
  auto left = parse_unary();
  if (!left) { tr.update_pos(pos_); return nullptr; }

  while (true) {
    TokenKind op = current().kind;
    int prec = binary_precedence(op);
    if (prec < min_prec) break;

    auto loc = current().location;
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_binary_expr",
          "consume operator " + tok_desc(current())});
    }
    advance(); // consume operator

    int next_min_prec = is_right_associative(op) ? prec : prec + 1;
    auto right = parse_binary_expr(next_min_prec);
    if (!right) { tr.update_pos(pos_); return nullptr; }

    left = make_expr(BinaryExpr{op, std::move(left), std::move(right)}, loc);
    tr.set_result("-> BinaryExpr");
  }

  tr.update_pos(pos_);
  return left;
}

ExprPtr Parser::parse_unary() {
  TraceRule tr(trace_, &trace_depth_, "parse_unary", pos_, "at " + tok_desc(current()));
  auto loc = current().location;

  if (check(TokenKind::Bang) || check(TokenKind::Minus) ||
      check(TokenKind::Plus) || check(TokenKind::Tilde) ||
      check(TokenKind::TypeOf)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_unary",
          "consume " + tok_desc(current())});
    }
    Token op = advance();
    auto operand = parse_unary(); // recursive for !!x, --5, etc.
    if (!operand) { tr.update_pos(pos_); return nullptr; }
    auto result = make_expr(UnaryExpr{op.kind, std::move(operand), true}, loc);
    tr.set_result("-> UnaryExpr (prefix)");
    tr.update_pos(pos_);
    return result;
  }

  auto result = parse_postfix();
  tr.update_pos(pos_);
  return result;
}

ExprPtr Parser::parse_postfix() {
  TraceRule tr(trace_, &trace_depth_, "parse_postfix", pos_, "at " + tok_desc(current()));
  auto expr = parse_call_or_member();
  if (!expr) { tr.update_pos(pos_); return nullptr; }

  if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
    auto loc = current().location;
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_postfix",
          "consume " + tok_desc(current())});
    }
    Token op = advance();
    auto result = make_expr(UnaryExpr{op.kind, std::move(expr), false}, loc);
    tr.set_result("-> UnaryExpr (postfix)");
    tr.update_pos(pos_);
    return result;
  }

  tr.update_pos(pos_);
  return expr;
}

ExprPtr Parser::parse_call_or_member() {
  TraceRule tr(trace_, &trace_depth_, "parse_call_or_member", pos_, "at " + tok_desc(current()));
  auto expr = parse_primary();
  if (!expr) { tr.update_pos(pos_); return nullptr; }

  while (true) {
    if (check(TokenKind::LeftParen)) {
      // Function call
      auto loc = current().location;
      auto args = parse_arguments();
      expr = make_expr(CallExpr{std::move(expr), std::move(args)}, loc);
      tr.set_result("-> CallExpr");
    } else if (match(TokenKind::Dot)) {
      // Dot member access
      auto loc = current().location;
      Token name = expect(TokenKind::Identifier, "Expected property name after '.'");
      expr = make_expr(MemberExpr{std::move(expr), name.lexeme, nullptr, false},
                       loc);
      tr.set_result("-> MemberExpr");
    } else if (match(TokenKind::LeftBracket)) {
      // Bracket member access
      auto loc = current().location;
      auto index = parse_expression();
      expect(TokenKind::RightBracket, "Expected ']' after index");
      expr = make_expr(
          MemberExpr{std::move(expr), "", std::move(index), true}, loc);
      tr.set_result("-> MemberExpr (computed)");
    } else {
      break;
    }
  }

  tr.update_pos(pos_);
  return expr;
}

// --- Primary expressions ---

ExprPtr Parser::parse_primary() {
  TraceRule tr(trace_, &trace_depth_, "parse_primary", pos_, "at " + tok_desc(current()));
  auto loc = current().location;

  // Number literal
  if (check(TokenKind::Number)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume " + tok_desc(current())});
    }
    Token tok = advance();
    double value = std::stod(tok.lexeme);
    auto result = make_expr(NumberLiteral{value}, loc);
    tr.set_result("-> NumberLiteral");
    tr.update_pos(pos_);
    return result;
  }

  // String literal
  if (check(TokenKind::String)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume " + tok_desc(current())});
    }
    Token tok = advance();
    std::string value = tok.lexeme.substr(1, tok.lexeme.size() - 2);
    auto result = make_expr(StringLiteral{value}, loc);
    tr.set_result("-> StringLiteral");
    tr.update_pos(pos_);
    return result;
  }

  // Boolean literals
  if (check(TokenKind::True)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume " + tok_desc(current())});
    }
    advance();
    auto result = make_expr(BooleanLiteral{true}, loc);
    tr.set_result("-> BooleanLiteral (true)");
    tr.update_pos(pos_);
    return result;
  }
  if (check(TokenKind::False)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume " + tok_desc(current())});
    }
    advance();
    auto result = make_expr(BooleanLiteral{false}, loc);
    tr.set_result("-> BooleanLiteral (false)");
    tr.update_pos(pos_);
    return result;
  }

  // Null / Undefined
  if (check(TokenKind::Null)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume " + tok_desc(current())});
    }
    advance();
    auto result = make_expr(NullLiteral{}, loc);
    tr.set_result("-> NullLiteral");
    tr.update_pos(pos_);
    return result;
  }
  if (check(TokenKind::Undefined)) {
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume " + tok_desc(current())});
    }
    advance();
    auto result = make_expr(UndefinedLiteral{}, loc);
    tr.set_result("-> UndefinedLiteral");
    tr.update_pos(pos_);
    return result;
  }

  // Array literal
  if (check(TokenKind::LeftBracket)) {
    auto result = parse_array_literal();
    tr.set_result("-> ArrayLiteral");
    tr.update_pos(pos_);
    return result;
  }

  // Template literal
  if (check(TokenKind::NoSubstitutionTemplate) ||
      check(TokenKind::TemplateHead)) {
    auto result = parse_template_literal();
    tr.set_result("-> TemplateLiteral");
    tr.update_pos(pos_);
    return result;
  }

  // Identifier or arrow function
  if (check(TokenKind::Identifier)) {
    // Single-param arrow: x => ...
    if (peek().kind == TokenKind::Arrow) {
      auto result = parse_arrow_function();
      tr.set_result("-> ArrowFunction");
      tr.update_pos(pos_);
      return result;
    }
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume " + tok_desc(current())});
    }
    Token tok = advance();
    auto result = make_expr(Identifier{tok.lexeme}, loc);
    tr.set_result("-> Identifier '" + tok.lexeme + "'");
    tr.update_pos(pos_);
    return result;
  }

  // Parenthesized expression, arrow function, or object literal
  if (check(TokenKind::LeftParen)) {
    if (is_arrow_function()) {
      auto result = parse_arrow_function();
      tr.set_result("-> ArrowFunction");
      tr.update_pos(pos_);
      return result;
    }
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_primary",
          "consume LeftParen '('"});
    }
    advance(); // consume '('
    auto expr = parse_expression();
    expect(TokenKind::RightParen, "Expected ')' after expression");
    tr.set_result("-> (grouped)");
    tr.update_pos(pos_);
    return expr;
  }

  // Object literal
  if (check(TokenKind::LeftBrace)) {
    auto result = parse_object_literal();
    tr.set_result("-> ObjectLiteral");
    tr.update_pos(pos_);
    return result;
  }

  error("Expected expression, got '" + current().lexeme + "'");
  synchronize();
  tr.update_pos(pos_);
  return nullptr;
}

// --- Stubs (implemented in later chunks) ---

StmtPtr Parser::parse_var_declaration() {
  TraceRule tr(trace_, &trace_depth_, "parse_var_declaration", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_var_declaration",
        "consume " + tok_desc(current())});
  }
  Token kind_tok = advance(); // consume let/const/var

  Token name = expect(TokenKind::Identifier, "Expected variable name");

  TypeAnnotationPtr type_ann = nullptr;
  if (match(TokenKind::Colon)) {
    type_ann = parse_type_annotation();
  }

  ExprPtr init = nullptr;
  if (match(TokenKind::Equal)) {
    init = parse_expression();
  }

  expect(TokenKind::Semicolon, "Expected ';' after variable declaration");

  auto result = make_stmt(
      VarDeclaration{kind_tok.kind, name.lexeme, std::move(type_ann),
                     std::move(init)},
      loc);
  tr.set_result("-> VarDeclaration");
  tr.update_pos(pos_);
  return result;
}
StmtPtr Parser::parse_block() {
  TraceRule tr(trace_, &trace_depth_, "parse_block", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  expect(TokenKind::LeftBrace, "Expected '{'");

  std::vector<StmtPtr> statements;
  while (!check(TokenKind::RightBrace) && !at_end()) {
    auto stmt = parse_statement();
    if (stmt) {
      statements.push_back(std::move(stmt));
    }
  }

  expect(TokenKind::RightBrace, "Expected '}'");
  auto result = make_stmt(BlockStmt{std::move(statements)}, loc);
  tr.set_result("-> BlockStmt");
  tr.update_pos(pos_);
  return result;
}
StmtPtr Parser::parse_if_statement() {
  TraceRule tr(trace_, &trace_depth_, "parse_if_statement", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_if_statement",
        "consume " + tok_desc(current())});
  }
  advance(); // consume 'if'

  expect(TokenKind::LeftParen, "Expected '(' after 'if'");
  auto condition = parse_expression();
  expect(TokenKind::RightParen, "Expected ')' after if condition");

  auto consequent = parse_statement();

  StmtPtr alternate = nullptr;
  if (match(TokenKind::Else)) {
    alternate = parse_statement();
  }

  auto result = make_stmt(
      IfStmt{std::move(condition), std::move(consequent), std::move(alternate)},
      loc);
  tr.set_result("-> IfStmt");
  tr.update_pos(pos_);
  return result;
}
StmtPtr Parser::parse_while_statement() {
  TraceRule tr(trace_, &trace_depth_, "parse_while_statement", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_while_statement",
        "consume " + tok_desc(current())});
  }
  advance(); // consume 'while'

  expect(TokenKind::LeftParen, "Expected '(' after 'while'");
  auto condition = parse_expression();
  expect(TokenKind::RightParen, "Expected ')' after while condition");

  auto body = parse_statement();

  auto result = make_stmt(WhileStmt{std::move(condition), std::move(body)}, loc);
  tr.set_result("-> WhileStmt");
  tr.update_pos(pos_);
  return result;
}
StmtPtr Parser::parse_for_statement() {
  TraceRule tr(trace_, &trace_depth_, "parse_for_statement", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_for_statement",
        "consume " + tok_desc(current())});
  }
  advance(); // consume 'for'

  expect(TokenKind::LeftParen, "Expected '(' after 'for'");

  // Initializer
  StmtPtr init = nullptr;
  if (match(TokenKind::Semicolon)) {
    // empty init
  } else if (check(TokenKind::Let) || check(TokenKind::Const) ||
             check(TokenKind::Var)) {
    init = parse_var_declaration();
  } else {
    init = parse_expression_statement();
  }

  // Condition
  ExprPtr condition = nullptr;
  if (!check(TokenKind::Semicolon)) {
    condition = parse_expression();
  }
  expect(TokenKind::Semicolon, "Expected ';' after for condition");

  // Update
  ExprPtr update = nullptr;
  if (!check(TokenKind::RightParen)) {
    update = parse_expression();
  }
  expect(TokenKind::RightParen, "Expected ')' after for clauses");

  auto body = parse_statement();

  auto result = make_stmt(
      ForStmt{std::move(init), std::move(condition), std::move(update),
              std::move(body)},
      loc);
  tr.set_result("-> ForStmt");
  tr.update_pos(pos_);
  return result;
}
StmtPtr Parser::parse_return_statement() {
  TraceRule tr(trace_, &trace_depth_, "parse_return_statement", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_return_statement",
        "consume " + tok_desc(current())});
  }
  advance(); // consume 'return'

  ExprPtr value = nullptr;
  if (!check(TokenKind::Semicolon) && !at_end()) {
    value = parse_expression();
  }

  expect(TokenKind::Semicolon, "Expected ';' after return");
  auto result = make_stmt(ReturnStmt{std::move(value)}, loc);
  tr.set_result("-> ReturnStmt");
  tr.update_pos(pos_);
  return result;
}
StmtPtr Parser::parse_function_declaration() {
  TraceRule tr(trace_, &trace_depth_, "parse_function_declaration", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_function_declaration",
        "consume " + tok_desc(current())});
  }
  advance(); // consume 'function'

  Token name = expect(TokenKind::Identifier, "Expected function name");
  auto params = parse_parameter_list();

  TypeAnnotationPtr return_type = nullptr;
  if (match(TokenKind::Colon)) {
    return_type = parse_type_annotation();
  }

  auto body = parse_block();

  auto result = make_stmt(
      FunctionDecl{name.lexeme, std::move(params), std::move(return_type),
                   std::move(body)},
      loc);
  tr.set_result("-> FunctionDecl '" + name.lexeme + "'");
  tr.update_pos(pos_);
  return result;
}
TypeAnnotationPtr Parser::parse_type_annotation() {
  TraceRule tr(trace_, &trace_depth_, "parse_type_annotation", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  Token name = expect(TokenKind::Identifier, "Expected type name");
  auto ann = std::make_unique<TypeAnnotation>();
  ann->name = name.lexeme;
  ann->location = loc;
  tr.set_result("-> TypeAnnotation '" + name.lexeme + "'");
  tr.update_pos(pos_);
  return ann;
}
std::vector<Parameter> Parser::parse_parameter_list() {
  TraceRule tr(trace_, &trace_depth_, "parse_parameter_list", pos_, "at " + tok_desc(current()));
  expect(TokenKind::LeftParen, "Expected '(' before parameters");
  std::vector<Parameter> params;

  if (!check(TokenKind::RightParen)) {
    do {
      auto loc = current().location;
      Token name = expect(TokenKind::Identifier, "Expected parameter name");
      TypeAnnotationPtr type_ann = nullptr;
      if (match(TokenKind::Colon)) {
        type_ann = parse_type_annotation();
      }
      params.push_back(Parameter{name.lexeme, std::move(type_ann), loc});
    } while (match(TokenKind::Comma));
  }

  expect(TokenKind::RightParen, "Expected ')' after parameters");
  tr.set_result(std::to_string(params.size()) + " params");
  tr.update_pos(pos_);
  return params;
}
std::vector<ExprPtr> Parser::parse_arguments() {
  TraceRule tr(trace_, &trace_depth_, "parse_arguments", pos_, "at " + tok_desc(current()));
  expect(TokenKind::LeftParen, "Expected '('");
  std::vector<ExprPtr> args;

  if (!check(TokenKind::RightParen)) {
    args.push_back(parse_expression());
    while (match(TokenKind::Comma)) {
      args.push_back(parse_expression());
    }
  }

  expect(TokenKind::RightParen, "Expected ')' after arguments");
  tr.set_result(std::to_string(args.size()) + " args");
  tr.update_pos(pos_);
  return args;
}
ExprPtr Parser::parse_arrow_function() {
  TraceRule tr(trace_, &trace_depth_, "parse_arrow_function", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  std::vector<Parameter> params;
  TypeAnnotationPtr return_type = nullptr;

  if (check(TokenKind::Identifier) && peek().kind == TokenKind::Arrow) {
    // Single-param form: x => ...
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_arrow_function",
          "consume param " + tok_desc(current())});
    }
    Token name = advance();
    params.push_back(Parameter{name.lexeme, nullptr, name.location});
  } else {
    // Parenthesized form: (params) => ...
    params = parse_parameter_list();
    if (match(TokenKind::Colon)) {
      return_type = parse_type_annotation();
    }
  }

  expect(TokenKind::Arrow, "Expected '=>'");

  ExprPtr result;
  if (check(TokenKind::LeftBrace)) {
    // Block body
    auto body = parse_block();
    result = make_expr(
        ArrowFunction{std::move(params), std::move(return_type),
                      std::move(body)},
        loc);
  } else {
    // Expression body
    auto body = parse_assignment();
    result = make_expr(
        ArrowFunction{std::move(params), std::move(return_type),
                      std::move(body)},
        loc);
  }
  tr.set_result("-> ArrowFunction");
  tr.update_pos(pos_);
  return result;
}
ExprPtr Parser::parse_array_literal() {
  TraceRule tr(trace_, &trace_depth_, "parse_array_literal", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_array_literal",
        "consume LeftBracket '['"});
  }
  advance(); // consume '['
  std::vector<ExprPtr> elements;

  if (!check(TokenKind::RightBracket)) {
    elements.push_back(parse_assignment());
    while (match(TokenKind::Comma)) {
      if (check(TokenKind::RightBracket)) break; // trailing comma
      elements.push_back(parse_assignment());
    }
  }

  expect(TokenKind::RightBracket, "Expected ']' after array elements");
  auto result = make_expr(ArrayLiteral{std::move(elements)}, loc);
  tr.set_result("-> ArrayLiteral (" + std::to_string(elements.size()) + " elements)");
  tr.update_pos(pos_);
  return result;
}
ExprPtr Parser::parse_object_literal() {
  TraceRule tr(trace_, &trace_depth_, "parse_object_literal", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  if (trace_) {
    trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_object_literal",
        "consume LeftBrace '{'"});
  }
  advance(); // consume '{'
  std::vector<ObjectProperty> properties;

  if (!check(TokenKind::RightBrace)) {
    do {
      auto key_loc = current().location;
      ExprPtr key;
      if (check(TokenKind::Identifier)) {
        if (trace_) {
          trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_object_literal",
              "consume key " + tok_desc(current())});
        }
        Token name = advance();
        key = make_expr(Identifier{name.lexeme}, key_loc);
      } else if (check(TokenKind::String)) {
        if (trace_) {
          trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_object_literal",
              "consume key " + tok_desc(current())});
        }
        Token str = advance();
        std::string value = str.lexeme.substr(1, str.lexeme.size() - 2);
        key = make_expr(StringLiteral{value}, key_loc);
      } else if (check(TokenKind::Number)) {
        if (trace_) {
          trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_object_literal",
              "consume key " + tok_desc(current())});
        }
        Token num = advance();
        key = make_expr(NumberLiteral{std::stod(num.lexeme)}, key_loc);
      } else {
        error("Expected property name");
        synchronize();
        tr.update_pos(pos_);
        return nullptr;
      }

      // Shorthand property: { x } is equivalent to { x: x }
      if (check(TokenKind::Comma) || check(TokenKind::RightBrace)) {
        // shorthand — key is an identifier, value is the same identifier
        auto &ident = std::get<Identifier>(*key);
        auto value = make_expr(Identifier{ident.name}, key_loc);
        properties.push_back(ObjectProperty{std::move(key), std::move(value)});
      } else {
        expect(TokenKind::Colon, "Expected ':' after property key");
        auto value = parse_assignment();
        properties.push_back(
            ObjectProperty{std::move(key), std::move(value)});
      }
    } while (match(TokenKind::Comma) && !check(TokenKind::RightBrace));
  }

  expect(TokenKind::RightBrace, "Expected '}' after object literal");
  auto result = make_expr(ObjectLiteral{std::move(properties)}, loc);
  tr.set_result("-> ObjectLiteral (" + std::to_string(properties.size()) + " props)");
  tr.update_pos(pos_);
  return result;
}
ExprPtr Parser::parse_template_literal() {
  TraceRule tr(trace_, &trace_depth_, "parse_template_literal", pos_, "at " + tok_desc(current()));
  auto loc = current().location;
  std::vector<std::string> quasis;
  std::vector<ExprPtr> expressions;

  if (check(TokenKind::NoSubstitutionTemplate)) {
    // Simple template with no interpolation: `hello`
    if (trace_) {
      trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_template_literal",
          "consume " + tok_desc(current())});
    }
    Token tok = advance();
    // Strip the backticks
    quasis.push_back(tok.lexeme.substr(1, tok.lexeme.size() - 2));
    auto result = make_expr(TemplateLiteral{std::move(quasis), std::move(expressions)},
                     loc);
    tr.set_result("-> TemplateLiteral (no substitution)");
    tr.update_pos(pos_);
    return result;
  }

  // Template with interpolation: `text${expr}text${expr}text`
  // TemplateHead: `text${
  Token head = expect(TokenKind::TemplateHead, "Expected template head");
  // Strip leading backtick and trailing ${
  quasis.push_back(head.lexeme.substr(1, head.lexeme.size() - 3));

  while (true) {
    expressions.push_back(parse_expression());

    if (check(TokenKind::TemplateTail)) {
      if (trace_) {
        trace_step(ParserStep{ParserStep::Type::ConsumeToken, pos_, 0, "parse_template_literal",
            "consume " + tok_desc(current())});
      }
      Token tail = advance();
      // Strip leading } and trailing backtick
      quasis.push_back(tail.lexeme.substr(1, tail.lexeme.size() - 2));
      break;
    }

    Token middle =
        expect(TokenKind::TemplateMiddle, "Expected template middle or tail");
    // Strip leading } and trailing ${
    quasis.push_back(middle.lexeme.substr(1, middle.lexeme.size() - 3));
  }

  auto result = make_expr(TemplateLiteral{std::move(quasis), std::move(expressions)},
                   loc);
  tr.set_result("-> TemplateLiteral (" + std::to_string(expressions.size()) + " exprs)");
  tr.update_pos(pos_);
  return result;
}
bool Parser::is_arrow_function() {
  // We're sitting on '(' — scan ahead to find matching ')' then check for '=>'
  size_t saved = pos_;
  int depth = 0;

  while (pos_ < tokens_.size() && tokens_[pos_].kind != TokenKind::EndOfFile) {
    if (tokens_[pos_].kind == TokenKind::LeftParen) {
      depth++;
    } else if (tokens_[pos_].kind == TokenKind::RightParen) {
      depth--;
      if (depth == 0) {
        pos_++;
        // Skip optional return type annotation: ): type =>
        if (pos_ < tokens_.size() &&
            tokens_[pos_].kind == TokenKind::Colon) {
          pos_++; // skip ':'
          // skip the type name
          if (pos_ < tokens_.size() &&
              tokens_[pos_].kind == TokenKind::Identifier) {
            pos_++;
          }
        }
        bool result = pos_ < tokens_.size() &&
                      tokens_[pos_].kind == TokenKind::Arrow;
        pos_ = saved;
        return result;
      }
    }
    pos_++;
  }

  pos_ = saved;
  return false;
}

} // namespace yatsi
