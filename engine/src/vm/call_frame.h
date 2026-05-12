#pragma once

#include "compiler/bytecode.h"

#include <cstdint>

namespace yatsi {

struct CallFrame {
  BytecodeFunction* function;
  uint32_t ip = 0;        // Instruction pointer (index into function->code)
  uint16_t base_register;  // Offset into the VM's flat register file
};

} // namespace yatsi
