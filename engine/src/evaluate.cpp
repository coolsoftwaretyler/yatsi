#include "yatsi.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "parser/ast_printer.h"
#include "typechecker/checker.h"
#include "compiler/compiler.h"
#include "runtime/gc.h"
#include "vm/vm.h"
#include <sstream>
#include <stdexcept>

EvalResult evaluate(const std::string& source) {
  try {
    // Break down a source string into a vector of tokens
    yatsi::Lexer lexer(std::string(source), "<eval>");
    auto tokens = lexer.tokenize();

    // Once we have our vector of tokens, we std::move them (convert to an rvalue so parser can take ownership without copying),
    // and then parse them into an abstract syntax tree, represented as a Program struct,
    // which holds a vector of StmtPtr (unique pointers containing Stmt, which can be any supported statement)
    yatsi::Parser parser(std::move(tokens), "<eval>");
    auto program = parser.parse();
    // If we run into errors, we combine all the error strings and return an EvalResult with the combined error message.
    // Since we are in the parsing stage, we set phase to 1.
    // TODO: maybe use an enum for the error stages.
    if (parser.has_errors()) {
        std::string combined;
        for (const auto& err : parser.errors()) {
            if (!combined.empty()) combined += "\n";
            combined += err;
        }
        return EvalResult{false, "SyntaxError", combined, 1, ""};
    }

    // Once we have a Program, we can actually compile it to bytecode.
    // We instantiate our GarbageCollector, so we can allocate on the heap during compilation.
    yatsi::GarbageCollector gc;
    // We give the compiler a reference to gc, then compile the program,
    // which gives us back our top-level BytecodeFunction
    yatsi::Compiler compiler(gc);
    auto func = compiler.compile(program);

    // A BytecodeFunction can be executed by our virtual machine.
    //
    std::ostringstream captured;
    yatsi::VM vm(gc, captured);
    // Once the VM has finished running the top level function,
    // it returns an InterpretResult, either `Ok` or `RuntimeError`
    auto result = vm.execute(func);

    // For our demo site, return an EvalResult struct based on the Interpret result.
    if (result == yatsi::InterpretResult::RuntimeError) {
        return EvalResult{false, "RuntimeError", "Runtime error", 2, captured.str()};
    }
    return EvalResult{true, "", "", 0, captured.str()};
  } catch (const std::exception& e) {
    return EvalResult{false, "InternalError", e.what(), 2, ""};
  } catch (...) {
    return EvalResult{false, "InternalError", "unknown exception", 2, ""};
  }
}

TypeCheckResult typecheck(const std::string& source) {
  try {
    yatsi::Lexer lexer(std::string(source), "<eval>");
    auto tokens = lexer.tokenize();

    yatsi::Parser parser(std::move(tokens), "<eval>");
    auto program = parser.parse();

    if (parser.has_errors()) {
      std::vector<std::string> errors;
      for (const auto& err : parser.errors()) {
        errors.push_back(err);
      }
      return TypeCheckResult{false, errors};
    }

    yatsi::TypeChecker checker;
    checker.check(program);

    return TypeCheckResult{true, checker.warnings()};
  } catch (const std::exception& e) {
    return TypeCheckResult{false, {std::string("InternalError: ") + e.what()}};
  } catch (...) {
    return TypeCheckResult{false, {"InternalError: unknown exception"}};
  }
}

std::string dump_typed_ast(const std::string& source) {
  yatsi::Lexer lexer(std::string(source), "<eval>");
  auto tokens = lexer.tokenize();

  yatsi::Parser parser(std::move(tokens), "<eval>");
  auto program = parser.parse();

  if (parser.has_errors()) {
    std::string combined;
    for (const auto& err : parser.errors()) {
      if (!combined.empty()) combined += "\n";
      combined += err;
    }
    return combined;
  }

  yatsi::TypeChecker checker;
  checker.check(program);

  std::ostringstream out;
  yatsi::print_typed_ast(program, out);
  return out.str();
}
