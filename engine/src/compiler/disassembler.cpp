#include "compiler/disassembler.h"

#include <iomanip>

namespace yatsi {

// Instruction format categories for display purposes
enum class InstrFormat { ABC, AB, ABx, AsBx, A, None };

static InstrFormat format_of(OpCode op) {
  switch (op) {
  // ABC: dest = op(src1, src2)
  case OpCode::Add:
  case OpCode::Sub:
  case OpCode::Mul:
  case OpCode::Div:
  case OpCode::Mod:
  case OpCode::Pow:
  case OpCode::AddNum:
  case OpCode::SubNum:
  case OpCode::MulNum:
  case OpCode::DivNum:
  case OpCode::ModNum:
  case OpCode::PowNum:
  case OpCode::BitAnd:
  case OpCode::BitOr:
  case OpCode::BitXor:
  case OpCode::ShiftLeft:
  case OpCode::ShiftRight:
  case OpCode::ShiftRightU:
  case OpCode::Equal:
  case OpCode::NotEqual:
  case OpCode::LessThan:
  case OpCode::LessEqual:
  case OpCode::GreaterThan:
  case OpCode::GreaterEqual:
  case OpCode::StrictEqual:
  case OpCode::StrictNotEqual:
  case OpCode::GetProp:
  case OpCode::SetProp:
  case OpCode::GetIndex:
  case OpCode::SetIndex:
  case OpCode::Call:
    return InstrFormat::ABC;

  // AB: dest = op(src)
  case OpCode::Move:
  case OpCode::Neg:
  case OpCode::NegNum:
  case OpCode::BitNot:
  case OpCode::Not:
  case OpCode::TypeOf:
  case OpCode::GetUpvalue:
  case OpCode::SetUpvalue:
  case OpCode::NewArray:
  case OpCode::Print:
    return InstrFormat::AB;

  // ABx: dest = constants[bx] or globals
  case OpCode::LoadConst:
  case OpCode::GetGlobal:
  case OpCode::SetGlobal:
  case OpCode::Closure:
    return InstrFormat::ABx;

  // AsBx: conditional jumps
  case OpCode::JumpIfTrue:
  case OpCode::JumpIfFalse:
    return InstrFormat::AsBx;

  // A only
  case OpCode::LoadNull:
  case OpCode::LoadUndef:
  case OpCode::LoadTrue:
  case OpCode::LoadFalse:
  case OpCode::NewObject:
  case OpCode::CloseUpvalue:
  case OpCode::Return:
    return InstrFormat::A;

  // No operands
  case OpCode::ReturnUndef:
    return InstrFormat::None;

  // Jump uses sBx but no A register
  case OpCode::Jump:
    return InstrFormat::AsBx;
  }
  return InstrFormat::None;
}

void disassemble(const BytecodeFunction& func, std::ostream& out) {
  out << "== " << (func.name.empty() ? "<script>" : func.name) << " ==\n";

  for (size_t i = 0; i < func.code.size(); ++i) {
    const auto& instr = func.code[i];
    OpCode op = instr.opcode();
    auto fmt = format_of(op);

    // Address
    out << std::setw(4) << std::setfill('0') << i << "  ";

    // Opcode name, left-aligned in 14 chars
    out << std::setfill(' ') << std::left << std::setw(14) << opcode_name(op)
        << std::right;

    // Operands
    switch (fmt) {
    case InstrFormat::ABC:
      out << "R" << (int)instr.a() << ", R" << (int)instr.b() << ", R"
          << (int)instr.c();
      break;

    case InstrFormat::AB:
      out << "R" << (int)instr.a() << ", R" << (int)instr.b();
      break;

    case InstrFormat::ABx:
      out << "R" << (int)instr.a() << ", K" << instr.bx();
      // Print constant value as comment
      if (instr.bx() < func.constants.size()) {
        out << "      ; " << func.constants[instr.bx()].to_debug_string();
      }
      break;

    case InstrFormat::AsBx:
      if (op == OpCode::Jump) {
        out << instr.sbx();
      } else {
        out << "R" << (int)instr.a() << ", " << instr.sbx();
      }
      break;

    case InstrFormat::A:
      out << "R" << (int)instr.a();
      break;

    case InstrFormat::None:
      break;
    }

    out << "\n";
  }

  // Constants summary
  if (!func.constants.empty()) {
    out << "Constants: [";
    for (size_t i = 0; i < func.constants.size(); ++i) {
      if (i > 0)
        out << ", ";
      out << func.constants[i].to_debug_string();
    }
    out << "]\n";
  }

  out << "Registers: " << (int)func.register_count << "\n";
}

} // namespace yatsi
