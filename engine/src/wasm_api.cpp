#include <emscripten/bind.h>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "common/common.h"
#include "compiler/compiler.h"
#include "compiler/disassembler.h"
#include "lexer/lexer.h"
#include "parser/ast_printer.h"
#include "parser/parser.h"
#include "runtime/gc.h"
#include "vm/vm.h"
#include "vm/vm_step.h"

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
  try {
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
  } catch (const std::exception& e) {
    return "{\"steps\":[],\"tokens\":[],\"error\":\"" + json_escape(e.what()) + "\"}";
  } catch (...) {
    return "{\"steps\":[],\"tokens\":[],\"error\":\"unknown exception\"}";
  }
}

std::string parse_traced(const std::string& source) {
  try {
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
  } catch (const std::exception& e) {
    return "{\"steps\":[],\"tokens\":[],\"ast\":\"\",\"errors\":[\"" + json_escape(e.what()) + "\"]}";
  } catch (...) {
    return "{\"steps\":[],\"tokens\":[],\"ast\":\"\",\"errors\":[\"unknown exception\"]}";
  }
}

std::string compile_traced(const std::string& source) {
  try {
  // Tokenize
  yatsi::Lexer lexer(std::string(source), "<playground>");
  auto tokens = lexer.tokenize();

  // Parse
  yatsi::Parser parser(tokens, "<playground>");
  auto program = parser.parse();

  if (parser.has_errors()) {
    std::string json = "{\"errors\":[";
    const auto& errs = parser.errors();
    for (size_t i = 0; i < errs.size(); ++i) {
      if (i > 0) json += ",";
      json += "\"" + json_escape(errs[i]) + "\"";
    }
    json += "]}";
    return json;
  }

  // AST string
  std::ostringstream ast_out;
  yatsi::print_ast(program, ast_out);
  std::string ast_str = ast_out.str();

  // Compile with tracing
  std::vector<yatsi::CompilerStep> trace;
  yatsi::GarbageCollector gc;
  yatsi::Compiler compiler(gc);
  compiler.enable_tracing(trace);
  auto func = compiler.compile(program);

  // Disassemble for full bytecode string
  std::ostringstream bytecode_out;
  yatsi::disassemble(func, bytecode_out);

  // Serialize steps
  std::string json = "{\"steps\":[";
  for (size_t i = 0; i < trace.size(); ++i) {
    const auto& step = trace[i];
    if (i > 0) json += ",";
    json += "{\"type\":\"";
    switch (step.type) {
    case yatsi::CompilerStep::Type::EnterNode:      json += "enterNode"; break;
    case yatsi::CompilerStep::Type::ExitNode:        json += "exitNode"; break;
    case yatsi::CompilerStep::Type::AllocRegister:   json += "allocRegister"; break;
    case yatsi::CompilerStep::Type::EmitInstruction: json += "emitInstruction"; break;
    case yatsi::CompilerStep::Type::AddConstant:     json += "addConstant"; break;
    case yatsi::CompilerStep::Type::PatchJump:       json += "patchJump"; break;
    case yatsi::CompilerStep::Type::PushLoop:        json += "pushLoop"; break;
    case yatsi::CompilerStep::Type::PopLoop:         json += "popLoop"; break;
    case yatsi::CompilerStep::Type::EnterFunction:   json += "enterFunction"; break;
    case yatsi::CompilerStep::Type::ExitFunction:    json += "exitFunction"; break;
    case yatsi::CompilerStep::Type::ResolveUpvalue:  json += "resolveUpvalue"; break;
    case yatsi::CompilerStep::Type::MarkCaptured:    json += "markCaptured"; break;
    case yatsi::CompilerStep::Type::AddUpvalue:      json += "addUpvalue"; break;
    case yatsi::CompilerStep::Type::ResolveGlobal:        json += "resolveGlobal"; break;
    case yatsi::CompilerStep::Type::ResolveLocal:        json += "resolveLocal"; break;
    case yatsi::CompilerStep::Type::ResolveLocalNotFound: json += "resolveLocalNotFound"; break;
    case yatsi::CompilerStep::Type::UpvalueDedup:        json += "upvalueDedup"; break;
    }
    json += "\",\"depth\":";
    json += std::to_string(step.depth);
    json += ",\"nodeType\":\"";
    json += json_escape(step.node_type);
    json += "\",\"desc\":\"";
    json += json_escape(step.description);
    json += "\"";
    if (step.instruction_index >= 0) {
      json += ",\"instrIndex\":";
      json += std::to_string(step.instruction_index);
    }
    if (step.register_id >= 0) {
      json += ",\"registerId\":";
      json += std::to_string(step.register_id);
    }
    if (step.constant_index >= 0) {
      json += ",\"constantIndex\":";
      json += std::to_string(step.constant_index);
    }
    if (step.patch_target >= 0) {
      json += ",\"patchTarget\":";
      json += std::to_string(step.patch_target);
    }
    if (step.source_line >= 0) {
      json += ",\"line\":";
      json += std::to_string(step.source_line);
    }
    if (step.source_column >= 0) {
      json += ",\"col\":";
      json += std::to_string(step.source_column);
    }
    if (!step.function_name.empty()) {
      json += ",\"functionName\":\"";
      json += json_escape(step.function_name);
      json += "\"";
    }
    if (!step.variable_name.empty()) {
      json += ",\"variableName\":\"";
      json += json_escape(step.variable_name);
      json += "\"";
    }
    if (step.upvalue_index >= 0) {
      json += ",\"upvalueIndex\":";
      json += std::to_string(step.upvalue_index);
    }
    if (step.is_local_upvalue) {
      json += ",\"isLocalUpvalue\":true";
    }
    if (step.param_count >= 0) {
      json += ",\"paramCount\":";
      json += std::to_string(step.param_count);
    }
    if (step.upvalue_count >= 0) {
      json += ",\"upvalueCount\":";
      json += std::to_string(step.upvalue_count);
    }
    json += "}";
  }

  // Serialize instructions
  json += "],\"instructions\":[";
  for (size_t i = 0; i < func.code.size(); ++i) {
    const auto& instr = func.code[i];
    if (i > 0) json += ",";
    json += "{\"opcode\":\"";
    json += json_escape(std::string(yatsi::opcode_name(instr.opcode())));
    json += "\",\"a\":";
    json += std::to_string(instr.a());
    json += ",\"b\":";
    json += std::to_string(instr.b());
    json += ",\"c\":";
    json += std::to_string(instr.c());
    json += ",\"bx\":";
    json += std::to_string(instr.bx());
    json += ",\"sbx\":";
    json += std::to_string(instr.sbx());
    json += "}";
  }

  // Serialize constants
  json += "],\"constants\":[";
  for (size_t i = 0; i < func.constants.size(); ++i) {
    if (i > 0) json += ",";
    json += "\"" + json_escape(func.constants[i].to_debug_string()) + "\"";
  }

  json += "],\"registerCount\":";
  json += std::to_string(func.register_count);

  // Serialize child functions recursively
  std::function<void(const yatsi::BytecodeFunction&, std::string&)> serialize_function;
  serialize_function = [&](const yatsi::BytecodeFunction& fn, std::string& out) {
    out += "{\"name\":\"";
    out += json_escape(fn.name);
    out += "\",\"paramCount\":";
    out += std::to_string(fn.param_count);
    out += ",\"registerCount\":";
    out += std::to_string(fn.register_count);

    // Instructions
    out += ",\"instructions\":[";
    for (size_t i = 0; i < fn.code.size(); ++i) {
      const auto& instr = fn.code[i];
      if (i > 0) out += ",";
      out += "{\"opcode\":\"";
      out += json_escape(std::string(yatsi::opcode_name(instr.opcode())));
      out += "\",\"a\":";
      out += std::to_string(instr.a());
      out += ",\"b\":";
      out += std::to_string(instr.b());
      out += ",\"c\":";
      out += std::to_string(instr.c());
      out += ",\"bx\":";
      out += std::to_string(instr.bx());
      out += ",\"sbx\":";
      out += std::to_string(instr.sbx());
      out += "}";
    }

    // Constants
    out += "],\"constants\":[";
    for (size_t i = 0; i < fn.constants.size(); ++i) {
      if (i > 0) out += ",";
      out += "\"" + json_escape(fn.constants[i].to_debug_string()) + "\"";
    }

    // Upvalue descriptors
    out += "],\"upvalueDescs\":[";
    for (size_t i = 0; i < fn.upvalue_descs.size(); ++i) {
      if (i > 0) out += ",";
      out += "{\"index\":";
      out += std::to_string(fn.upvalue_descs[i].index);
      out += ",\"isLocal\":";
      out += fn.upvalue_descs[i].is_local ? "true" : "false";
      out += "}";
    }

    // Nested functions (recursive)
    out += "],\"functions\":[";
    for (size_t i = 0; i < fn.functions.size(); ++i) {
      if (i > 0) out += ",";
      serialize_function(fn.functions[i], out);
    }
    out += "]}";
  };

  json += ",\"functions\":[";
  for (size_t i = 0; i < func.functions.size(); ++i) {
    if (i > 0) json += ",";
    serialize_function(func.functions[i], json);
  }
  json += "]";

  json += ",\"ast\":\"";
  json += json_escape(ast_str);

  json += "\",\"bytecode\":\"";
  json += json_escape(bytecode_out.str());

  json += "\",\"errors\":[]}";
  return json;
  } catch (const std::exception& e) {
    return "{\"errors\":[\"" + json_escape(e.what()) + "\"]}";
  } catch (...) {
    return "{\"errors\":[\"unknown exception\"]}";
  }
}

std::string execute_traced(const std::string& source) {
  try {
  // Tokenize
  yatsi::Lexer lexer(std::string(source), "<playground>");
  auto tokens = lexer.tokenize();

  // Parse
  yatsi::Parser parser(tokens, "<playground>");
  auto program = parser.parse();

  if (parser.has_errors()) {
    std::string json = "{\"error\":\"";
    const auto& errs = parser.errors();
    for (size_t i = 0; i < errs.size(); ++i) {
      if (i > 0) json += "; ";
      json += json_escape(errs[i]);
    }
    json += "\",\"steps\":[],\"program\":null,\"output\":\"\"}";
    return json;
  }

  // Compile
  yatsi::GarbageCollector gc;
  yatsi::Compiler compiler(gc);
  auto func = compiler.compile(program);

  // Disassemble
  std::ostringstream bytecode_out;
  yatsi::disassemble(func, bytecode_out);

  // Execute with tracing
  std::vector<yatsi::VMStep> trace;
  std::ostringstream captured;
  yatsi::VM vm(gc, captured);
  vm.enable_tracing(trace);
  auto result = vm.execute(func);

  // Build JSON output
  std::string json = "{";

  // Serialize the program (reuse compile_traced's serialization approach)
  std::function<void(const yatsi::BytecodeFunction&, std::string&)> serialize_function;
  serialize_function = [&](const yatsi::BytecodeFunction& fn, std::string& out) {
    out += "{\"name\":\"";
    out += json_escape(fn.name);
    out += "\",\"paramCount\":";
    out += std::to_string(fn.param_count);
    out += ",\"registerCount\":";
    out += std::to_string(fn.register_count);
    out += ",\"instructions\":[";
    for (size_t i = 0; i < fn.code.size(); ++i) {
      const auto& instr = fn.code[i];
      if (i > 0) out += ",";
      out += "{\"opcode\":\"";
      out += json_escape(std::string(yatsi::opcode_name(instr.opcode())));
      out += "\",\"a\":";
      out += std::to_string(instr.a());
      out += ",\"b\":";
      out += std::to_string(instr.b());
      out += ",\"c\":";
      out += std::to_string(instr.c());
      out += ",\"bx\":";
      out += std::to_string(instr.bx());
      out += ",\"sbx\":";
      out += std::to_string(instr.sbx());
      out += "}";
    }
    out += "],\"constants\":[";
    for (size_t i = 0; i < fn.constants.size(); ++i) {
      if (i > 0) out += ",";
      out += "\"" + json_escape(fn.constants[i].to_debug_string()) + "\"";
    }
    out += "],\"upvalueDescs\":[";
    for (size_t i = 0; i < fn.upvalue_descs.size(); ++i) {
      if (i > 0) out += ",";
      out += "{\"index\":";
      out += std::to_string(fn.upvalue_descs[i].index);
      out += ",\"isLocal\":";
      out += fn.upvalue_descs[i].is_local ? "true" : "false";
      out += "}";
    }
    out += "],\"functions\":[";
    for (size_t i = 0; i < fn.functions.size(); ++i) {
      if (i > 0) out += ",";
      serialize_function(fn.functions[i], out);
    }
    out += "]}";
  };

  // Program object (top-level function)
  json += "\"program\":{\"instructions\":[";
  for (size_t i = 0; i < func.code.size(); ++i) {
    const auto& instr = func.code[i];
    if (i > 0) json += ",";
    json += "{\"opcode\":\"";
    json += json_escape(std::string(yatsi::opcode_name(instr.opcode())));
    json += "\",\"a\":";
    json += std::to_string(instr.a());
    json += ",\"b\":";
    json += std::to_string(instr.b());
    json += ",\"c\":";
    json += std::to_string(instr.c());
    json += ",\"bx\":";
    json += std::to_string(instr.bx());
    json += ",\"sbx\":";
    json += std::to_string(instr.sbx());
    json += "}";
  }
  json += "],\"constants\":[";
  for (size_t i = 0; i < func.constants.size(); ++i) {
    if (i > 0) json += ",";
    json += "\"" + json_escape(func.constants[i].to_debug_string()) + "\"";
  }
  json += "],\"registerCount\":";
  json += std::to_string(func.register_count);
  json += ",\"functions\":[";
  for (size_t i = 0; i < func.functions.size(); ++i) {
    if (i > 0) json += ",";
    serialize_function(func.functions[i], json);
  }
  json += "],\"bytecode\":\"";
  json += json_escape(bytecode_out.str());
  json += "\"}";

  // Serialize VM steps
  json += ",\"steps\":[";
  for (size_t i = 0; i < trace.size(); ++i) {
    const auto& step = trace[i];
    if (i > 0) json += ",";
    json += "{\"type\":\"";
    switch (step.type) {
    case yatsi::VMStep::Type::Execute:        json += "execute"; break;
    case yatsi::VMStep::Type::Call:            json += "call"; break;
    case yatsi::VMStep::Type::Return:          json += "return"; break;
    case yatsi::VMStep::Type::CaptureUpvalue:  json += "captureUpvalue"; break;
    case yatsi::VMStep::Type::CloseUpvalue:    json += "closeUpvalue"; break;
    case yatsi::VMStep::Type::ReadUpvalue:     json += "readUpvalue"; break;
    case yatsi::VMStep::Type::WriteUpvalue:    json += "writeUpvalue"; break;
    }
    json += "\",\"opcode\":\"";
    json += json_escape(step.opcode_name);
    json += "\",\"a\":";
    json += std::to_string(step.a);
    json += ",\"b\":";
    json += std::to_string(step.b);
    json += ",\"c\":";
    json += std::to_string(step.c);
    json += ",\"bx\":";
    json += std::to_string(step.bx);
    json += ",\"sbx\":";
    json += std::to_string(step.sbx);
    json += ",\"ip\":";
    json += std::to_string(step.ip);
    json += ",\"functionName\":\"";
    json += json_escape(step.function_name);
    json += "\",\"callDepth\":";
    json += std::to_string(step.call_depth);
    json += ",\"baseRegister\":";
    json += std::to_string(step.base_register);
    json += ",\"desc\":\"";
    json += json_escape(step.description);
    json += "\"";

    // Register writes
    if (!step.reg_writes.empty()) {
      json += ",\"regWrites\":[";
      for (size_t j = 0; j < step.reg_writes.size(); ++j) {
        if (j > 0) json += ",";
        json += "{\"index\":";
        json += std::to_string(step.reg_writes[j].index);
        json += ",\"value\":\"";
        json += json_escape(step.reg_writes[j].value);
        json += "\"}";
      }
      json += "]";
    }

    // Upvalue metadata
    if (step.upvalue_index >= 0) {
      json += ",\"upvalueIndex\":";
      json += std::to_string(step.upvalue_index);
    }
    if (!step.upvalue_var_name.empty()) {
      json += ",\"upvalueVarName\":\"";
      json += json_escape(step.upvalue_var_name);
      json += "\"";
    }
    if (step.type == yatsi::VMStep::Type::ReadUpvalue ||
        step.type == yatsi::VMStep::Type::WriteUpvalue ||
        step.type == yatsi::VMStep::Type::CaptureUpvalue ||
        step.type == yatsi::VMStep::Type::CloseUpvalue) {
      json += ",\"upvalueIsOpen\":";
      json += step.upvalue_is_open ? "true" : "false";
    }
    if (!step.upvalue_value.empty()) {
      json += ",\"upvalueValue\":\"";
      json += json_escape(step.upvalue_value);
      json += "\"";
    }
    if (step.closure_func_index >= 0) {
      json += ",\"closureFuncIndex\":";
      json += std::to_string(step.closure_func_index);
    }
    if (step.upvalue_count >= 0) {
      json += ",\"upvalueCount\":";
      json += std::to_string(step.upvalue_count);
    }

    json += "}";
  }
  json += "]";

  // Output
  json += ",\"output\":\"";
  json += json_escape(captured.str());
  json += "\"";

  // Error
  if (result == yatsi::InterpretResult::RuntimeError) {
    json += ",\"error\":\"Runtime error\"";
  } else {
    json += ",\"error\":null";
  }

  json += "}";
  return json;
  } catch (const std::exception& e) {
    return "{\"error\":\"" + json_escape(e.what()) + "\",\"steps\":[],\"program\":null,\"output\":\"\"}";
  } catch (...) {
    return "{\"error\":\"unknown exception\",\"steps\":[],\"program\":null,\"output\":\"\"}";
  }
}

std::string run_pipeline(const std::string& source) {
  try {
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
  } catch (const std::exception& e) {
    return "{\"ok\":false,\"tokens\":[],\"errors\":[\"" + json_escape(e.what()) + "\"]}";
  } catch (...) {
    return "{\"ok\":false,\"tokens\":[],\"errors\":[\"unknown exception\"]}";
  }
}

} // namespace

EMSCRIPTEN_BINDINGS(yatsi_playground) {
  emscripten::function("run_pipeline", &run_pipeline);
  emscripten::function("tokenize_traced", &tokenize_traced);
  emscripten::function("parse_traced", &parse_traced);
  emscripten::function("compile_traced", &compile_traced);
  emscripten::function("execute_traced", &execute_traced);
}
