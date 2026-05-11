#pragma once

#include <string>

struct EvalResult {
    bool success;
    std::string error_type;
    std::string error_message;
    int phase; // 0 = none, 1 = parse, 2 = runtime
    std::string console_output;
};

EvalResult evaluate(const std::string& source);
