#include <emscripten/bind.h>
#include <sstream>
#include <string>

#include "common/common.h"
#include "compiler/compiler.h"
#include "compiler/disassembler.h"
#include "lexer/lexer.h"
#include "parser/ast_printer.h"
#include "parser/parser.h"
#include "runtime/gc.h"
#include "vm/vm.h"

namespace {

// --- JSON helpers ---

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':  out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n";  break;
    case '\r': out += "\\r";  break;
    case '\t': out += "\\t";  break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

// --- API functions ---

std::string tokenize_traced(const std::string& source) {
  yatsi::Lexer lexer(std::string(source), "<playground>");
  std::vector<yatsi::LexerStep> trace;
  lexer.enable_tracing(trace);
  auto tokens = lexer.tokenize();

  // Build line_starts table for byte offset computation
  std::vector<size_t> line_starts;
  line_starts.push_back(0);
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\n') {
      line_starts.push_back(i + 1);
    }
  }

  // Serialize steps
  std::string json = "{\"steps\":[";
  for (size_t i = 0; i < trace.size(); ++i) {
    const auto& step = trace[i];
    if (i > 0) json += ",";
    json += "{\"type\":\"";
    switch (step.type) {
    case yatsi::LexerStep::Type::Advance:          json += "advance"; break;
    case yatsi::LexerStep::Type::Match:             json += "match"; break;
    case yatsi::LexerStep::Type::MatchFail:         json += "matchFail"; break;
    case yatsi::LexerStep::Type::SkipWhitespace:    json += "skipWs"; break;
    case yatsi::LexerStep::Type::SkipLineComment:   json += "skipLineComment"; break;
    case yatsi::LexerStep::Type::SkipBlockComment:  json += "skipBlockComment"; break;
    case yatsi::LexerStep::Type::ScanStart:         json += "scanStart"; break;
    case yatsi::LexerStep::Type::EmitToken:         json += "emit"; break;
    case yatsi::LexerStep::Type::Eof:               json += "eof"; break;
    }
    json += "\",\"pos\":";
    json += std::to_string(step.pos);
    json += ",\"endPos\":";
    json += std::to_string(step.end_pos);
    if (step.ch != '\0') {
      json += ",\"ch\":\"";
      json += json_escape(std::string(1, step.ch));
      json += "\"";
    }
    if (step.expected != '\0') {
      json += ",\"expected\":\"";
      json += json_escape(std::string(1, step.expected));
      json += "\"";
    }
    if (step.token_index >= 0) {
      json += ",\"tokenIndex\":";
      json += std::to_string(step.token_index);
    }
    // For emit steps, include the token kind and lexeme
    if (step.type == yatsi::LexerStep::Type::EmitToken && step.token_index >= 0 &&
        static_cast<size_t>(step.token_index) < tokens.size()) {
      const auto& tok = tokens[step.token_index];
      json += ",\"kind\":\"";
      json += json_escape(std::string(yatsi::token_kind_to_string(tok.kind)));
      json += "\",\"lexeme\":\"";
      json += json_escape(tok.lexeme);
      json += "\"";
    }
    json += ",\"desc\":\"";
    json += json_escape(step.description);
    json += "\"}";
  }
  json += "],\"tokens\":[";

  // Serialize tokens with spans
  for (size_t i = 0; i < tokens.size(); ++i) {
    const auto& tok = tokens[i];
    if (i > 0) json += ",";

    size_t start = 0;
    if (tok.location.line >= 1 &&
        static_cast<size_t>(tok.location.line - 1) < line_starts.size()) {
      start = line_starts[tok.location.line - 1] +
              (tok.location.column > 0 ? tok.location.column - 1 : 0);
    }
    size_t end = start;
    if (tok.kind != yatsi::TokenKind::EndOfFile &&
        tok.kind != yatsi::TokenKind::Error) {
      end = start + tok.lexeme.size();
    }

    json += "{\"kind\":\"";
    json += json_escape(std::string(yatsi::token_kind_to_string(tok.kind)));
    json += "\",\"lexeme\":\"";
    json += json_escape(tok.lexeme);
    json += "\",\"line\":";
    json += std::to_string(tok.location.line);
    json += ",\"column\":";
    json += std::to_string(tok.location.column);
    json += ",\"start\":";
    json += std::to_string(start);
    json += ",\"end\":";
    json += std::to_string(end);
    json += "}";
  }
  json += "]}";
  return json;
}

std::string parse_traced(const std::string& source) {
  // Tokenize with spans
  yatsi::Lexer lexer(std::string(source), "<playground>");
  auto tokens = lexer.tokenize();

  // Build line_starts table for byte offset computation
  std::vector<size_t> line_starts;
  line_starts.push_back(0);
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\n') {
      line_starts.push_back(i + 1);
    }
  }

  // Parse with tracing
  std::vector<yatsi::ParserStep> trace;
  yatsi::Parser parser(tokens, "<playground>");
  parser.enable_tracing(trace);
  auto program = parser.parse();

  // AST string
  std::string ast_str;
  if (!parser.has_errors()) {
    std::ostringstream ast_out;
    yatsi::print_ast(program, ast_out);
    ast_str = ast_out.str();
  }

  // Serialize
  std::string json = "{\"steps\":[";
  for (size_t i = 0; i < trace.size(); ++i) {
    const auto& step = trace[i];
    if (i > 0) json += ",";
    json += "{\"type\":\"";
    switch (step.type) {
    case yatsi::ParserStep::Type::EnterRule:    json += "enterRule"; break;
    case yatsi::ParserStep::Type::ExitRule:     json += "exitRule"; break;
    case yatsi::ParserStep::Type::ConsumeToken: json += "consumeToken"; break;
    case yatsi::ParserStep::Type::TryMatch:     json += "tryMatch"; break;
    case yatsi::ParserStep::Type::ProduceNode:  json += "produceNode"; break;
    case yatsi::ParserStep::Type::Error:        json += "error"; break;
    case yatsi::ParserStep::Type::Synchronize:  json += "synchronize"; break;
    }
    json += "\",\"tokenPos\":";
    json += std::to_string(step.token_pos);
    json += ",\"depth\":";
    json += std::to_string(step.depth);
    json += ",\"rule\":\"";
    json += json_escape(step.rule);
    json += "\",\"desc\":\"";
    json += json_escape(step.description);
    json += "\"}";
  }

  json += "],\"tokens\":[";
  for (size_t i = 0; i < tokens.size(); ++i) {
    const auto& tok = tokens[i];
    if (i > 0) json += ",";

    size_t start = 0;
    if (tok.location.line >= 1 &&
        static_cast<size_t>(tok.location.line - 1) < line_starts.size()) {
      start = line_starts[tok.location.line - 1] +
              (tok.location.column > 0 ? tok.location.column - 1 : 0);
    }
    size_t end = start;
    if (tok.kind != yatsi::TokenKind::EndOfFile &&
        tok.kind != yatsi::TokenKind::Error) {
      end = start + tok.lexeme.size();
    }

    json += "{\"kind\":\"";
    json += json_escape(std::string(yatsi::token_kind_to_string(tok.kind)));
    json += "\",\"lexeme\":\"";
    json += json_escape(tok.lexeme);
    json += "\",\"line\":";
    json += std::to_string(tok.location.line);
    json += ",\"column\":";
    json += std::to_string(tok.location.column);
    json += ",\"start\":";
    json += std::to_string(start);
    json += ",\"end\":";
    json += std::to_string(end);
    json += "}";
  }

  json += "],\"ast\":\"";
  json += json_escape(ast_str);
  json += "\",\"errors\":[";
  const auto& errs = parser.errors();
  for (size_t i = 0; i < errs.size(); ++i) {
    if (i > 0) json += ",";
    json += "\"" + json_escape(errs[i]) + "\"";
  }
  json += "]}";
  return json;
}

std::string run_pipeline(const std::string& source) {
  // Tokenize
  yatsi::Lexer lexer(std::string(source), "<playground>");
  auto tokens = lexer.tokenize();

  std::string tokens_json = "[";
  for (size_t i = 0; i < tokens.size(); ++i) {
    const auto& tok = tokens[i];
    if (i > 0) tokens_json += ",";
    tokens_json += "{\"kind\":\"";
    tokens_json +=
        json_escape(std::string(yatsi::token_kind_to_string(tok.kind)));
    tokens_json += "\",\"lexeme\":\"";
    tokens_json += json_escape(tok.lexeme);
    tokens_json += "\",\"line\":";
    tokens_json += std::to_string(tok.location.line);
    tokens_json += ",\"column\":";
    tokens_json += std::to_string(tok.location.column);
    tokens_json += "}";
  }
  tokens_json += "]";

  // Parse
  yatsi::Parser parser(std::move(tokens), "<playground>");
  auto program = parser.parse();

  if (parser.has_errors()) {
    std::string json = "{\"ok\":false,\"tokens\":" + tokens_json;
    json += ",\"errors\":[";
    const auto& errs = parser.errors();
    for (size_t i = 0; i < errs.size(); ++i) {
      if (i > 0) json += ",";
      json += "\"" + json_escape(errs[i]) + "\"";
    }
    json += "]}";
    return json;
  }

  std::ostringstream ast_out;
  yatsi::print_ast(program, ast_out);

  // Compile
  yatsi::GarbageCollector gc;
  yatsi::Compiler compiler(gc);
  auto func = compiler.compile(program);

  std::ostringstream bytecode_out;
  yatsi::disassemble(func, bytecode_out);

  // Execute
  std::ostringstream captured;
  yatsi::VM vm(gc, captured);
  auto result = vm.execute(func);

  std::string json = "{\"ok\":true,\"tokens\":" + tokens_json;
  json += ",\"ast\":\"" + json_escape(ast_out.str()) + "\"";
  json += ",\"bytecode\":\"" + json_escape(bytecode_out.str()) + "\"";
  if (result == yatsi::InterpretResult::RuntimeError) {
    json += ",\"output\":\"\",\"runtimeError\":true";
  } else {
    json += ",\"output\":\"" + json_escape(captured.str()) + "\"";
  }
  json += "}";
  return json;
}

} // namespace

EMSCRIPTEN_BINDINGS(yatsi_playground) {
  emscripten::function("run_pipeline", &run_pipeline);
  emscripten::function("tokenize_traced", &tokenize_traced);
  emscripten::function("parse_traced", &parse_traced);
}
