#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "common/source_location.h"

namespace yatsi {

enum class TokenKind : uint8_t {
  // Literals
  Number,
  String,
  TemplateHead,           // `text${
  TemplateMiddle,         // }text${
  TemplateTail,           // }text`
  NoSubstitutionTemplate, // `text`
  RegexLiteral,

  // Keywords — values
  True,
  False,
  Null,
  Undefined,

  // Keywords — declarations
  Let,
  Const,
  Var,
  Function,
  Class,
  Extends,
  Super,
  New,
  This,
  Return,

  // Keywords — control flow
  If,
  Else,
  While,
  For,
  Do,
  Break,
  Continue,
  Switch,
  Case,
  Default,

  // Keywords — error handling
  Throw,
  Try,
  Catch,
  Finally,

  // Keywords — operators
  TypeOf,
  InstanceOf,
  In,
  Of,
  Void,
  Delete,

  // Keywords — modules
  Import,
  Export,

  // Keywords — async
  Async,
  Await,
  Yield,

  // Keywords — type system
  Enum,
  Interface,
  Type,
  As,
  Implements,
  Declare,
  Readonly,

  // Operators — arithmetic
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  StarStar,

  // Operators — assignment
  Equal,
  PlusEqual,
  MinusEqual,
  StarEqual,
  SlashEqual,
  PercentEqual,

  // Operators — comparison
  EqualEqual,
  EqualEqualEqual,
  BangEqual,
  BangEqualEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,

  // Operators — logical
  AmpersandAmpersand,
  PipePipe,
  Bang,
  QuestionQuestion,

  // Operators — bitwise
  Ampersand,
  Pipe,
  Caret,
  Tilde,
  LessLess,
  GreaterGreater,
  GreaterGreaterGreater,

  // Operators — increment/decrement
  PlusPlus,
  MinusMinus,

  // Operators — misc
  Question,
  Arrow,
  Dot,
  DotDotDot,

  // Delimiters
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  LeftBracket,
  RightBracket,
  Semicolon,
  Comma,
  Colon,

  // Special
  Identifier,
  EndOfFile,
  Error,
};

// Represents a single token produced by the lexer.
struct Token {
  TokenKind kind;
  std::string lexeme;
  SourceLocation location;
};

std::string_view token_kind_to_string(TokenKind kind);
TokenKind lookup_keyword(const std::string &identifier);

} // namespace yatsi
