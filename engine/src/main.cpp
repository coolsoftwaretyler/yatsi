#include "yatsi.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char* argv[]) {
    std::string source;

    if (argc >= 2) {
        std::ifstream file(argv[1]);
        if (!file) {
            std::cerr << "error: cannot open file: " << argv[1] << "\n";
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
