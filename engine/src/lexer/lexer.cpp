#include "lexer/lexer.h"
#include "lexer/token.h"

#include <cctype>
#include <utility>

namespace yatsi {

static bool is_hex_digit(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

Lexer::Lexer(std::string source, std::string filename)
    : source_(std::move(source)), filename_(std::move(filename)) {}

void Lexer::enable_tracing(std::vector<LexerStep> &trace) {
  trace_ = &trace;
}

void Lexer::trace_step(LexerStep step) {
  if (trace_) {
    trace_->push_back(std::move(step));
  }
}

char Lexer::current() const {
  if (at_end())
    return '\0';
  return source_[pos_];
}

char Lexer::peek() const {
  if (pos_ + 1 >= source_.size())
    return '\0';
  return source_[pos_ + 1];
}

char Lexer::advance() {
  size_t old_pos = pos_;
  char c = source_[pos_];
  pos_++;
  if (c == '\n') {
    line_++;
    column_ = 1;
  } else {
    column_++;
  }
  if (trace_ && !in_skip_whitespace_) {
    std::string desc = "advance() -> '";
    desc += c;
    desc += "'";
    trace_step({LexerStep::Type::Advance, old_pos, pos_, c, '\0', -1,
                std::move(desc)});
  }
  return c;
}

bool Lexer::at_end() const { return pos_ >= source_.size(); }

bool Lexer::match(char expected) {
  if (at_end()) {
    if (trace_ && !in_skip_whitespace_) {
      std::string desc = "match('";
      desc += expected;
      desc += "') -> false (at end)";
      trace_step({LexerStep::Type::MatchFail, pos_, pos_, '\0', expected, -1,
                  std::move(desc)});
    }
    return false;
  }
  if (source_[pos_] != expected) {
    if (trace_ && !in_skip_whitespace_) {
      std::string desc = "match('";
      desc += expected;
      desc += "') -> false (saw '";
      desc += source_[pos_];
      desc += "')";
      trace_step({LexerStep::Type::MatchFail, pos_, pos_, source_[pos_],
                  expected, -1, std::move(desc)});
    }
    return false;
  }
  size_t old_pos = pos_;
  advance();
  if (trace_ && !in_skip_whitespace_) {
    // The advance() already traced an Advance step; replace it with a Match step
    // by popping the Advance and emitting Match instead
    if (!trace_->empty() && trace_->back().type == LexerStep::Type::Advance) {
      trace_->pop_back();
    }
    std::string desc = "match('";
    desc += expected;
    desc += "') -> true";
    trace_step({LexerStep::Type::Match, old_pos, pos_, expected, expected, -1,
                std::move(desc)});
  }
  return true;
}

void Lexer::skip_whitespace() {
  in_skip_whitespace_ = true;
  while (!at_end()) {
    char c = current();
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      size_t start = pos_;
      while (!at_end() && (current() == ' ' || current() == '\t' ||
                           current() == '\n' || current() == '\r')) {
        advance();
      }
      if (trace_) {
        size_t count = pos_ - start;
        std::string desc = "skip " + std::to_string(count) + " whitespace";
        trace_step({LexerStep::Type::SkipWhitespace, start, pos_, '\0', '\0',
                    -1, std::move(desc)});
      }
    } else if (c == '/' && peek() == '/') {
      size_t start = pos_;
      // Line comment: skip to end of line
      while (!at_end() && current() != '\n')
        advance();
      if (trace_) {
        trace_step({LexerStep::Type::SkipLineComment, start, pos_, '\0', '\0',
                    -1, "skip // comment"});
      }
    } else if (c == '/' && peek() == '*') {
      size_t start = pos_;
      // Block comment: skip to */
      advance(); // skip /
      advance(); // skip *
      while (!at_end()) {
        if (current() == '*' && peek() == '/') {
          advance(); // skip *
          advance(); // skip /
          break;
        }
        advance();
      }
      if (trace_) {
        trace_step({LexerStep::Type::SkipBlockComment, start, pos_, '\0', '\0',
                    -1, "skip /* comment */"});
      }
    } else {
      break;
    }
  }
  in_skip_whitespace_ = false;
}

/**
 * Disambiguate '/' as the start of a regex literal vs. division operator.
 * A regex literal can only appear in contexts where an operator or delimiter is
 * expected, not after an identifier, number, string, etc.
 */
bool Lexer::can_precede_regex() const {
  switch (prev_kind_) {
  case TokenKind::Identifier:
  case TokenKind::Number:
  case TokenKind::String:
  case TokenKind::RegexLiteral:
  case TokenKind::NoSubstitutionTemplate:
  case TokenKind::TemplateTail:
  case TokenKind::True:
  case TokenKind::False:
  case TokenKind::Null:
  case TokenKind::Undefined:
  case TokenKind::This:
  case TokenKind::Super:
  case TokenKind::RightParen:
  case TokenKind::RightBracket:
  case TokenKind::PlusPlus:
  case TokenKind::MinusMinus:
    return false;
  default:
    return true;
  }
}

Token Lexer::make_token(TokenKind kind, const std::string &lexeme) {
  return Token{kind, lexeme, {filename_, line_, column_}};
}

Token Lexer::error_token(const std::string &message) {
  return Token{TokenKind::Error, message, {filename_, line_, column_}};
}

Token Lexer::scan_token() {
  skip_whitespace();

  if (at_end()) {
    trace_step({LexerStep::Type::Eof, pos_, pos_, '\0', '\0', -1,
                "end of file"});
    return make_token(TokenKind::EndOfFile, "");
  }

  size_t token_start = pos_;

  // ScanStart: about to begin scanning a new token
  if (trace_) {
    std::string desc = "start scanning token at '";
    desc += current();
    desc += "'";
    trace_step({LexerStep::Type::ScanStart, pos_, pos_, current(), '\0', -1,
                std::move(desc)});
  }

  int start_line = line_;
  int start_col = column_;
  char c = advance();

  auto token = [&](TokenKind kind, const std::string &lexeme) -> Token {
    return Token{kind, lexeme, {filename_, start_line, start_col}};
  };

  Token result = [&]() -> Token {
    switch (c) {
    // Delimiters
    case '(':
      return token(TokenKind::LeftParen, "(");
    case ')':
      return token(TokenKind::RightParen, ")");
    case '{':
      if (!template_brace_stack_.empty()) {
        template_brace_stack_.back()++;
      }
      return token(TokenKind::LeftBrace, "{");
    case '}':
      if (!template_brace_stack_.empty()) {
        if (template_brace_stack_.back() == 0) {
          template_brace_stack_.pop_back();
          return scan_template_part(false);
        }
        template_brace_stack_.back()--;
      }
      return token(TokenKind::RightBrace, "}");
    case '[':
      return token(TokenKind::LeftBracket, "[");
    case ']':
      return token(TokenKind::RightBracket, "]");
    case ';':
      return token(TokenKind::Semicolon, ";");
    case ',':
      return token(TokenKind::Comma, ",");
    case ':':
      return token(TokenKind::Colon, ":");
    case '~':
      return token(TokenKind::Tilde, "~");

    // Operators
    case '+':
      if (match('+'))
        return token(TokenKind::PlusPlus, "++");
      if (match('='))
        return token(TokenKind::PlusEqual, "+=");
      return token(TokenKind::Plus, "+");
    case '-':
      if (match('-'))
        return token(TokenKind::MinusMinus, "--");
      if (match('='))
        return token(TokenKind::MinusEqual, "-=");
      return token(TokenKind::Minus, "-");
    case '*':
      if (match('*'))
        return token(TokenKind::StarStar, "**");
      if (match('='))
        return token(TokenKind::StarEqual, "*=");
      return token(TokenKind::Star, "*");
    case '/':
      if (match('='))
        return token(TokenKind::SlashEqual, "/=");
      if (can_precede_regex())
        return scan_regex();
      return token(TokenKind::Slash, "/");
    case '%':
      if (match('='))
        return token(TokenKind::PercentEqual, "%=");
      return token(TokenKind::Percent, "%");
    case '=':
      if (match('=')) {
        if (match('='))
          return token(TokenKind::EqualEqualEqual, "===");
        return token(TokenKind::EqualEqual, "==");
      }
      if (match('>'))
        return token(TokenKind::Arrow, "=>");
      return token(TokenKind::Equal, "=");
    case '!':
      if (match('=')) {
        if (match('='))
          return token(TokenKind::BangEqualEqual, "!==");
        return token(TokenKind::BangEqual, "!=");
      }
      return token(TokenKind::Bang, "!");
    case '<':
      if (match('<'))
        return token(TokenKind::LessLess, "<<");
      if (match('='))
        return token(TokenKind::LessEqual, "<=");
      return token(TokenKind::Less, "<");
    case '>':
      if (match('>')) {
        if (match('>'))
          return token(TokenKind::GreaterGreaterGreater, ">>>");
        return token(TokenKind::GreaterGreater, ">>");
      }
      if (match('='))
        return token(TokenKind::GreaterEqual, ">=");
      return token(TokenKind::Greater, ">");
    case '&':
      if (match('&'))
        return token(TokenKind::AmpersandAmpersand, "&&");
      return token(TokenKind::Ampersand, "&");
    case '|':
      if (match('|'))
        return token(TokenKind::PipePipe, "||");
      return token(TokenKind::Pipe, "|");
    case '^':
      return token(TokenKind::Caret, "^");
    case '?':
      if (match('?'))
        return token(TokenKind::QuestionQuestion, "??");
      return token(TokenKind::Question, "?");
    case '.':
      if (match('.')) {
        if (match('.'))
          return token(TokenKind::DotDotDot, "...");
        return token(TokenKind::Dot, ".");
      }
      if (!at_end() && std::isdigit(current())) {
        return scan_number();
      }
      return token(TokenKind::Dot, ".");

    case '\'':
    case '"':
      return scan_string();

    case '`':
      return scan_template_part(true);

    default:
      if (std::isdigit(c)) {
        return scan_number();
      }
      if (std::isalpha(c) || c == '_' || c == '$') {
        return scan_identifier();
      }
      {
        std::string msg = "Unexpected character: ";
        msg += c;
        return Token{TokenKind::Error, msg, {filename_, start_line, start_col}};
      }
    }
  }();

  // EmitToken trace step — emitted for every token produced
  // token_index is set by the caller (tokenize()) after this returns
  if (trace_) {
    std::string desc = "Emit ";
    desc += token_kind_to_string(result.kind);
    desc += " '";
    desc += result.lexeme;
    desc += "'";
    trace_step({LexerStep::Type::EmitToken, token_start, pos_, '\0', '\0', -1,
                std::move(desc)});
  }

  return result;
}

Token Lexer::scan_number() {
  int start_line = line_;
  int start_col = column_ - 1;
  size_t start = pos_ - 1;
  bool is_float = false;

  auto make_error = [&](const std::string &msg) -> Token {
    return Token{TokenKind::Error, msg, {filename_, start_line, start_col}};
  };

  // Result codes for consume_digits
  enum DigitResult { Ok, NoDigits, LeadingSep, TrailingSep, ConsecutiveSep };

  // Consumes digits (validated by is_valid) and underscores.
  // already_have_digit: true if a digit was consumed before this call.
  auto consume_digits = [&](auto is_valid,
                            bool already_have_digit) -> DigitResult {
    bool have_digit = already_have_digit;
    while (!at_end()) {
      if (current() == '_') {
        if (!have_digit) {
          advance();
          return LeadingSep;
        }
        advance();
        if (at_end()) {
          return TrailingSep;
        }
        if (current() == '_') {
          advance();
          return ConsecutiveSep;
        }
        if (!is_valid(current())) {
          return TrailingSep;
        }
      } else if (is_valid(current())) {
        have_digit = true;
        advance();
      } else {
        break;
      }
    }
    return have_digit ? Ok : NoDigits;
  };

  auto sep_error = [&](DigitResult r) -> Token {
    switch (r) {
    case LeadingSep:
      return make_error("Numeric separator not allowed at start of number");
    case TrailingSep:
      return make_error("Numeric separator not allowed at end of number");
    case ConsecutiveSep:
      return make_error("Multiple consecutive numeric separators not allowed");
    default:
      return make_error("unreachable");
    }
  };

  auto is_decimal = [](char ch) { return std::isdigit(ch) != 0; };
  auto is_hex = [](char ch) { return std::isxdigit(ch) != 0; };
  auto is_octal = [](char ch) { return ch >= '0' && ch <= '7'; };
  auto is_binary = [](char ch) { return ch == '0' || ch == '1'; };

  char first = source_[start];

  // Leading-dot float: .5, .123
  if (first == '.') {
    is_float = true;
    auto r = consume_digits(is_decimal, false);
    if (r != Ok)
      return sep_error(r);
  }
  // Hex, octal, binary
  else if (first == '0' && !at_end() &&
           (current() == 'x' || current() == 'X')) {
    advance(); // consume x/X
    auto r = consume_digits(is_hex, false);
    if (r != Ok) {
      if (r == NoDigits)
        return make_error(
            "Hexadecimal literal must have at least one digit after '0x'");
      return sep_error(r);
    }
    if (!at_end() && current() == 'n')
      advance(); // BigInt
    std::string text = source_.substr(start, pos_ - start);
    return Token{TokenKind::Number, text, {filename_, start_line, start_col}};
  } else if (first == '0' && !at_end() &&
             (current() == 'o' || current() == 'O')) {
    advance(); // consume o/O
    auto r = consume_digits(is_octal, false);
    if (r != Ok) {
      if (r == NoDigits)
        return make_error(
            "Octal literal must have at least one digit after '0o'");
      return sep_error(r);
    }
    if (!at_end() && current() == 'n')
      advance(); // BigInt
    std::string text = source_.substr(start, pos_ - start);
    return Token{TokenKind::Number, text, {filename_, start_line, start_col}};
  } else if (first == '0' && !at_end() &&
             (current() == 'b' || current() == 'B')) {
    advance(); // consume b/B
    auto r = consume_digits(is_binary, false);
    if (r != Ok) {
      if (r == NoDigits)
        return make_error(
            "Binary literal must have at least one digit after '0b'");
      return sep_error(r);
    }
    if (!at_end() && current() == 'n')
      advance(); // BigInt
    std::string text = source_.substr(start, pos_ - start);
    return Token{TokenKind::Number, text, {filename_, start_line, start_col}};
  }
  // Decimal integer (first digit already consumed by scan_token)
  else {
    auto r = consume_digits(is_decimal, true);
    if (r != Ok)
      return sep_error(r);
  }

  // Fractional part (decimal/leading-dot only, not for hex/octal/binary which
  // returned early)
  if (!is_float && !at_end() && current() == '.' && peek() != '.') {
    is_float = true;
    advance(); // consume '.'
    // Digits after dot are optional (2. is valid)
    consume_digits(is_decimal, true);
  }

  // Exponent part
  if (!at_end() && (current() == 'e' || current() == 'E')) {
    is_float = true;
    advance(); // consume e/E
    if (!at_end() && (current() == '+' || current() == '-')) {
      advance(); // consume sign
    }
    auto r = consume_digits(is_decimal, false);
    if (r != Ok) {
      if (r == NoDigits)
        return make_error("Exponent part must have at least one digit");
      return sep_error(r);
    }
  }

  // BigInt suffix
  if (!at_end() && current() == 'n') {
    if (is_float) {
      advance(); // consume 'n' so it's part of the error token
      return make_error("BigInt literal cannot be a floating-point number");
    }
    advance(); // consume 'n'
  }

  std::string text = source_.substr(start, pos_ - start);
  return Token{TokenKind::Number, text, {filename_, start_line, start_col}};
}

Token Lexer::scan_string() {
  int start_line = line_;
  int start_col = column_ - 1;
  size_t start = pos_ - 1;
  char quote = source_[start];

  auto make_error = [&](const std::string &msg) -> Token {
    return Token{TokenKind::Error, msg, {filename_, start_line, start_col}};
  };

  while (!at_end()) {
    char c = current();

    if (c == quote) {
      advance(); // consume closing quote
      std::string text = source_.substr(start, pos_ - start);
      return Token{TokenKind::String, text, {filename_, start_line, start_col}};
    }

    if (c == '\n') {
      return make_error("Unterminated string literal");
    }

    if (c == '\\') {
      advance(); // consume backslash
      if (at_end()) {
        return make_error("Unterminated string literal");
      }

      char esc = current();
      switch (esc) {
      case 'n':
      case 't':
      case 'r':
      case 'b':
      case 'f':
      case 'v':
      case '0':
      case '\\':
      case '\'':
      case '"':
        advance();
        break;

      case '\n':
        // Line continuation
        advance();
        break;

      case 'x': {
        advance(); // consume 'x'
        for (int i = 0; i < 2; i++) {
          if (at_end() || !is_hex_digit(current())) {
            return make_error("Invalid hexadecimal escape sequence");
          }
          advance();
        }
        break;
      }

      case 'u': {
        advance(); // consume 'u'
        if (!at_end() && current() == '{') {
          advance(); // consume '{'
          int digits = 0;
          while (!at_end() && is_hex_digit(current())) {
            advance();
            digits++;
          }
          if (digits == 0 || at_end() || current() != '}') {
            return make_error("Invalid Unicode escape sequence");
          }
          advance(); // consume '}'
        } else {
          for (int i = 0; i < 4; i++) {
            if (at_end() || !is_hex_digit(current())) {
              return make_error("Invalid Unicode escape sequence");
            }
            advance();
          }
        }
        break;
      }

      default: {
        std::string msg = "Unknown escape sequence: \\";
        msg += esc;
        return make_error(msg);
      }
      }
      continue;
    }

    advance(); // regular character
  }

  return make_error("Unterminated string literal");
}

Token Lexer::scan_template_part(bool is_head) {
  // is_head == true: opening char was ` (already consumed by scan_token)
  // is_head == false: opening char was } (already consumed by scan_token)
  int start_line = line_;
  int start_col = column_ - 1;
  size_t start = pos_ - 1;

  auto make_error = [&](const std::string &msg) -> Token {
    return Token{TokenKind::Error, msg, {filename_, start_line, start_col}};
  };

  while (!at_end()) {
    char c = current();

    if (c == '`') {
      advance(); // consume closing backtick
      std::string text = source_.substr(start, pos_ - start);
      TokenKind kind =
          is_head ? TokenKind::NoSubstitutionTemplate : TokenKind::TemplateTail;
      return Token{kind, text, {filename_, start_line, start_col}};
    }

    if (c == '$' && peek() == '{') {
      advance(); // consume '$'
      advance(); // consume '{'
      template_brace_stack_.push_back(0);
      std::string text = source_.substr(start, pos_ - start);
      TokenKind kind =
          is_head ? TokenKind::TemplateHead : TokenKind::TemplateMiddle;
      return Token{kind, text, {filename_, start_line, start_col}};
    }

    if (c == '\\') {
      advance(); // consume backslash
      if (at_end()) {
        return make_error("Unterminated template literal");
      }

      char esc = current();
      switch (esc) {
      case 'n':
      case 't':
      case 'r':
      case 'b':
      case 'f':
      case 'v':
      case '0':
      case '\\':
      case '\'':
      case '"':
      case '`':
      case '$':
        advance();
        break;

      case '\n':
        advance();
        break;

      case 'x': {
        advance(); // consume 'x'
        for (int i = 0; i < 2; i++) {
          if (at_end() || !is_hex_digit(current())) {
            return make_error("Invalid hexadecimal escape sequence");
          }
          advance();
        }
        break;
      }

      case 'u': {
        advance(); // consume 'u'
        if (!at_end() && current() == '{') {
          advance(); // consume '{'
          int digits = 0;
          while (!at_end() && is_hex_digit(current())) {
            advance();
            digits++;
          }
          if (digits == 0 || at_end() || current() != '}') {
            return make_error("Invalid Unicode escape sequence");
          }
          advance(); // consume '}'
        } else {
          for (int i = 0; i < 4; i++) {
            if (at_end() || !is_hex_digit(current())) {
              return make_error("Invalid Unicode escape sequence");
            }
            advance();
          }
        }
        break;
      }

      default: {
        std::string msg = "Unknown escape sequence: \\";
        msg += esc;
        return make_error(msg);
      }
      }
      continue;
    }

    // Raw newlines are allowed in template literals
    advance();
  }

  return make_error("Unterminated template literal");
}

Token Lexer::scan_identifier() {
  int start_line = line_;
  int start_col = column_ - 1;
  size_t start = pos_ - 1;

  while (!at_end() &&
         (std::isalnum(current()) || current() == '_' || current() == '$')) {
    advance();
  }

  std::string text = source_.substr(start, pos_ - start);
  TokenKind kind = lookup_keyword(text);
  return Token{kind, text, {filename_, start_line, start_col}};
}

Token Lexer::scan_regex() {
  int start_line = line_;
  int start_col = column_ - 1;
  size_t start = pos_ - 1;

  bool in_char_class = false;

  auto make_error = [&](const std::string &msg) -> Token {
    return Token{TokenKind::Error, msg, {filename_, start_line, start_col}};
  };

  while (!at_end()) {
    if (current() == '\n') {
      return make_error("Unterminated regular expression");
    }

    if (current() == '\\') {
      advance();
      if (at_end() || current() == '\n') {
        return make_error("Unterminated regular expression");
      }
      advance();
      continue;
    }

    if (current() == '[' && !in_char_class) {
      in_char_class = true;
      advance();
      continue;
    }

    if (current() == ']' && in_char_class) {
      in_char_class = false;
      advance();
      continue;
    }

    if (current() == '/' && !in_char_class) {
      advance();
      while (!at_end() && std::isalpha(current())) {
        advance();
      }
      std::string text = source_.substr(start, pos_ - start);
      return Token{
          TokenKind::RegexLiteral, text, {filename_, start_line, start_col}};
    }

    advance();
  }

  return make_error("Unterminated regular expression");
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  int token_index = 0;
  while (true) {
    Token tok = scan_token();
    prev_kind_ = tok.kind;
    // Patch the token_index on the EmitToken step we just emitted
    if (trace_ && !trace_->empty() &&
        trace_->back().type == LexerStep::Type::EmitToken) {
      trace_->back().token_index = token_index;
    }
    tokens.push_back(tok);
    token_index++;
    if (tok.kind == TokenKind::EndOfFile)
      break;
  }
  return tokens;
}

} // namespace yatsi
