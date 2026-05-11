#include "compiler/bytecode.h"

namespace yatsi {

const char* opcode_name(OpCode op) {
  switch (op) {
  // Constants & moves
  case OpCode::LoadConst: return "LoadConst";
  case OpCode::LoadNull: return "LoadNull";
  case OpCode::LoadUndef: return "LoadUndef";
  case OpCode::LoadTrue: return "LoadTrue";
  case OpCode::LoadFalse: return "LoadFalse";
  case OpCode::Move: return "Move";

  // Arithmetic (generic)
  case OpCode::Add: return "Add";
  case OpCode::Sub: return "Sub";
  case OpCode::Mul: return "Mul";
  case OpCode::Div: return "Div";
  case OpCode::Mod: return "Mod";
  case OpCode::Pow: return "Pow";
  case OpCode::Neg: return "Neg";

  // Arithmetic (typed)
  case OpCode::AddNum: return "AddNum";
  case OpCode::SubNum: return "SubNum";
  case OpCode::MulNum: return "MulNum";
  case OpCode::DivNum: return "DivNum";
  case OpCode::ModNum: return "ModNum";
  case OpCode::PowNum: return "PowNum";
  case OpCode::NegNum: return "NegNum";

  // Bitwise
  case OpCode::BitAnd: return "BitAnd";
  case OpCode::BitOr: return "BitOr";
  case OpCode::BitXor: return "BitXor";
  case OpCode::BitNot: return "BitNot";
  case OpCode::ShiftLeft: return "ShiftLeft";
  case OpCode::ShiftRight: return "ShiftRight";
  case OpCode::ShiftRightU: return "ShiftRightU";

  // Comparison
  case OpCode::Equal: return "Equal";
  case OpCode::NotEqual: return "NotEqual";
  case OpCode::LessThan: return "LessThan";
  case OpCode::LessEqual: return "LessEqual";
  case OpCode::GreaterThan: return "GreaterThan";
  case OpCode::GreaterEqual: return "GreaterEqual";
  case OpCode::StrictEqual: return "StrictEqual";
  case OpCode::StrictNotEqual: return "StrictNotEqual";

  // Logical / unary
  case OpCode::Not: return "Not";
  case OpCode::TypeOf: return "TypeOf";

  // Control flow
  case OpCode::Jump: return "Jump";
  case OpCode::JumpIfTrue: return "JumpIfTrue";
  case OpCode::JumpIfFalse: return "JumpIfFalse";

  // Global variables
  case OpCode::GetGlobal: return "GetGlobal";
  case OpCode::SetGlobal: return "SetGlobal";

  // Upvalues
  case OpCode::GetUpvalue: return "GetUpvalue";
  case OpCode::SetUpvalue: return "SetUpvalue";
  case OpCode::CloseUpvalue: return "CloseUpvalue";

  // Functions
  case OpCode::Closure: return "Closure";
  case OpCode::Call: return "Call";
  case OpCode::Return: return "Return";
  case OpCode::ReturnUndef: return "ReturnUndef";

  // Objects
  case OpCode::NewObject: return "NewObject";
  case OpCode::GetProp: return "GetProp";
  case OpCode::SetProp: return "SetProp";
  case OpCode::GetIndex: return "GetIndex";
  case OpCode::SetIndex: return "SetIndex";

  // Arrays
  case OpCode::NewArray: return "NewArray";

  // Built-ins
  case OpCode::Print: return "Print";
  }
  return "<unknown>";
}

} // namespace yatsi
