#pragma once

#include "common/value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace yatsi {

enum class OpCode : uint8_t {
  // Constants & moves
  LoadConst,
  LoadNull,
  LoadUndef,
  LoadTrue,
  LoadFalse,
  Move,

  // Arithmetic (generic)
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Pow,
  Neg,

  // Arithmetic (typed — no runtime type checks)
  AddNum,
  SubNum,
  MulNum,
  DivNum,
  ModNum,
  PowNum,
  NegNum,

  // Bitwise
  BitAnd,
  BitOr,
  BitXor,
  BitNot,
  ShiftLeft,
  ShiftRight,
  ShiftRightU,

  // Comparison
  Equal,
  NotEqual,
  LessThan,
  LessEqual,
  GreaterThan,
  GreaterEqual,
  StrictEqual,
  StrictNotEqual,

  // Logical / unary
  Not,
  TypeOf,

  // Control flow
  Jump,
  JumpIfTrue,
  JumpIfFalse,

  // Global variables
  GetGlobal,
  SetGlobal,

  // Upvalues (closures)
  GetUpvalue,
  SetUpvalue,
  CloseUpvalue,

  // Functions
  Closure,
  Call,
  Return,
  ReturnUndef,

  // Objects
  NewObject,
  GetProp,
  SetProp,
  GetIndex,
  SetIndex,

  // Arrays
  NewArray,

  // Built-ins
  Print, // Print A B — print R[A] through R[A+B-1] to stdout, newline after
};

const char* opcode_name(OpCode op);

// --- Instruction encoding ---
//
// 32-bit fixed-width. Three formats:
//
//   ABC:  [opcode:8][A:8][B:8][C:8]
//   ABx:  [opcode:8][A:8][Bx:16]       (unsigned)
//   AsBx: [opcode:8][A:8][sBx:16]      (signed)

struct Instruction {
  uint32_t encoded;

  // Decode fields
  OpCode opcode() const {
    return static_cast<OpCode>((encoded >> 24) & 0xFF);
  }
  uint8_t a() const { return static_cast<uint8_t>((encoded >> 16) & 0xFF); }
  uint8_t b() const { return static_cast<uint8_t>((encoded >> 8) & 0xFF); }
  uint8_t c() const { return static_cast<uint8_t>(encoded & 0xFF); }
  uint16_t bx() const { return static_cast<uint16_t>(encoded & 0xFFFF); }
  int16_t sbx() const { return static_cast<int16_t>(encoded & 0xFFFF); }

  // Encode
  static Instruction abc(OpCode op, uint8_t a, uint8_t b, uint8_t c) {
    return {(static_cast<uint32_t>(op) << 24) |
            (static_cast<uint32_t>(a) << 16) |
            (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(c)};
  }

  static Instruction abx(OpCode op, uint8_t a, uint16_t bx) {
    return {(static_cast<uint32_t>(op) << 24) |
            (static_cast<uint32_t>(a) << 16) | static_cast<uint32_t>(bx)};
  }

  static Instruction asbx(OpCode op, uint8_t a, int16_t sbx) {
    return {(static_cast<uint32_t>(op) << 24) |
            (static_cast<uint32_t>(a) << 16) |
            (static_cast<uint32_t>(static_cast<uint16_t>(sbx)))};
  }
};

struct UpvalueDesc {
  uint8_t index;
  bool is_local;
};

struct BytecodeFunction {
  std::string name;
  std::vector<Instruction> code;
  std::vector<Value> constants;
  std::vector<BytecodeFunction> functions;
  std::vector<UpvalueDesc> upvalue_descs;
  uint8_t register_count = 0;
  uint8_t param_count = 0;
  std::vector<uint32_t> line_numbers;
};

} // namespace yatsi
