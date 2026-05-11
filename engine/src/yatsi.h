#pragma once

#include <string>

// Return type for a TypeScript evaluation, intended to be passed back to JavaScript via Emscripten as a simple object.
struct EvalResult {
    bool success;
    std::string error_type;
    std::string error_message;
    int phase; // 0 = none, 1 = parse, 2 = runtime
    std::string console_output;
};

EvalResult evaluate(const std::string& source);
