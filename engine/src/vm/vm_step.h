#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yatsi {

struct VMStep {
  enum class Type {
    Execute,        // Normal instruction execution
    Call,           // Function call (push frame)
    Return,         // Function return (pop frame)
    CaptureUpvalue, // Upvalue captured (in Closure op)
    CloseUpvalue,   // Upvalue closed to heap
    ReadUpvalue,    // GetUpvalue
    WriteUpvalue,   // SetUpvalue
  };

  Type type;
  std::string opcode_name;    // e.g. "LoadConst"
  uint8_t a = 0, b = 0, c = 0;
  uint16_t bx = 0;
  int16_t sbx = 0;
  uint32_t ip = 0;            // Instruction pointer before execution
  std::string function_name;  // Current function name
  int call_depth = 0;         // call_stack_.size()
  uint16_t base_register = 0; // Current frame's base register
  std::string description;    // Human-readable summary

  // Register snapshot — only registers written by this instruction
  struct RegEntry {
    uint8_t index;
    std::string value;
  };
  std::vector<RegEntry> reg_writes;

  // Closure/upvalue metadata (only for closure-related steps)
  int upvalue_index = -1;
  std::string upvalue_var_name;
  bool upvalue_is_open = true;
  std::string upvalue_value;
  int closure_func_index = -1;
  int upvalue_count = -1;
};

} // namespace yatsi
