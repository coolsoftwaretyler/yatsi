#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lexer/token.h"

namespace yatsi {

struct LexerStep {
  enum class Type : uint8_t {
    Advance,
    Match,
    MatchFail,
    SkipWhitespace,
    SkipLineComment,
    SkipBlockComment,
    ScanStart,
    EmitToken,
    Eof
  };
  Type type;
  size_t pos;        // cursor position before this step
  size_t end_pos;    // cursor position after this step
  char ch;           // character involved (for advance/match)
  char expected;     // expected char (for match/matchFail)
  int token_index;   // -1 unless EmitToken
  std::string description;
};

class Lexer {
public:
  Lexer(std::string source, std::string filename);
  std::vector<Token> tokenize();
  void enable_tracing(std::vector<LexerStep> &trace);

private:
  std::string source_;
  std::string filename_;
  size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;
  TokenKind prev_kind_ = TokenKind::EndOfFile;

  std::vector<LexerStep> *trace_ = nullptr;
  bool in_skip_whitespace_ = false;

  void trace_step(LexerStep step);

  char current() const;
  char peek() const;
  char advance();
  bool at_end() const;
  bool match(char expected);
  void skip_whitespace();
  bool can_precede_regex() const;
  Token make_token(TokenKind kind, const std::string &lexeme);
  Token error_token(const std::string &message);
  Token scan_token();
  Token scan_identifier();
  Token scan_number();
  Token scan_string();
  Token scan_template_part(bool is_head);
  Token scan_regex();

  std::vector<int> template_brace_stack_;
};

} // namespace yatsi
