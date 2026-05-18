#include "yatsi.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char* argv[]) {
    std::string source;
    bool mode_typecheck = false;
    bool mode_dump_types = false;

    // Parse flags and find the source file argument
    const char* source_file = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--typecheck") == 0) {
            mode_typecheck = true;
        } else if (std::strcmp(argv[i], "--dump-types") == 0) {
            mode_dump_types = true;
        } else {
            source_file = argv[i];
        }
    }

    if (source_file) {
        std::ifstream file(source_file);
        if (!file) {
            std::cerr << "error: cannot open file: " << source_file << "\n";
            return 1;
        }
        std::ostringstream buf;
        buf << file.rdbuf();
        source = buf.str();
    } else {
        std::ostringstream buf;
        buf << std::cin.rdbuf();
        source = buf.str();
    }

    if (mode_typecheck) {
        auto result = typecheck(source);
        for (const auto& warning : result.warnings) {
            std::cout << warning << "\n";
        }
        return result.success ? 0 : 1;
    }

    if (mode_dump_types) {
        std::string output = dump_typed_ast(source);
        std::cout << output;
        return 0;
    }

    auto result = evaluate(source);

    if (!result.console_output.empty()) {
        std::cout << result.console_output;
    }

    if (!result.success) {
        std::cerr << result.error_type << ": " << result.error_message << "\n";
        return 1;
    }

    return 0;
}
