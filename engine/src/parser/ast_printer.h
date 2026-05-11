#pragma once

#include "parser/ast.h"

#include <ostream>

namespace yatsi {

void print_ast(const Program& program, std::ostream& out);

} // namespace yatsi
