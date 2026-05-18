#pragma once

#include <string>
#include <vector>

// Return type for a TypeScript evaluation, intended to be passed back to JavaScript via Emscripten as a simple object.
struct EvalResult {
    bool success;
    std::string error_type;
    std::string error_message;
    int phase; // 0 = none, 1 = parse, 2 = runtime
    std::string console_output;
};

EvalResult evaluate(const std::string& source);

// Return type for type-check-only mode (no compilation/VM).
struct TypeCheckResult {
    bool success;
    std::vector<std::string> warnings;
};

TypeCheckResult typecheck(const std::string& source);

// Run the type checker and return the annotated AST as a string.
std::string dump_typed_ast(const std::string& source);
