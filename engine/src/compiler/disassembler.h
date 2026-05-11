#pragma once

#include "compiler/bytecode.h"

#include <ostream>

namespace yatsi {

void disassemble(const BytecodeFunction& func, std::ostream& out);

} // namespace yatsi
