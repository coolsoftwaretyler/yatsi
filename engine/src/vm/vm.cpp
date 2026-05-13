#include "vm/vm.h"

#include "runtime/js_function.h"
#include "runtime/js_string.h"

#include <cmath>
#include <iostream>

namespace yatsi {

VM::VM(GarbageCollector &gc) : gc_(gc), out_(std::cout) {
  registers_.fill(Value::undefined());
}

VM::VM(GarbageCollector &gc, std::ostream &out) : gc_(gc), out_(out) {
  registers_.fill(Value::undefined());
}

InterpretResult VM::execute(BytecodeFunction &func) {
  // Push the top-level script frame
  CallFrame frame;
  frame.function = &func;
  frame.ip = 0;
  frame.base_register = 0;
  call_stack_.push_back(frame);

  while (current_frame().ip < current_frame().function->code.size()) {
    auto &cf = current_frame();
    Instruction instr = cf.function->code[cf.ip];
    cf.ip++;

    switch (instr.opcode()) {

      // --- Constants & moves ---

    case OpCode::LoadConst:
      reg(instr.a()) = cf.function->constants[instr.bx()];
      break;

    case OpCode::LoadNull:
      reg(instr.a()) = Value::null();
      break;

    case OpCode::LoadUndef:
      reg(instr.a()) = Value::undefined();
      break;

    case OpCode::LoadTrue:
      reg(instr.a()) = Value::boolean(true);
      break;

    case OpCode::LoadFalse:
      reg(instr.a()) = Value::boolean(false);
      break;

    case OpCode::Move:
      reg(instr.a()) = reg(instr.b());
      break;

      // --- Arithmetic (generic) ---

    case OpCode::Add: {
      Value &b = reg(instr.b());
      Value &c = reg(instr.c());
      // String concatenation if either operand is a string
      if (b.is_string() || c.is_string()) {
        std::u16string result;
        if (b.is_string())
          result += b.as_string()->data();
        else
          result += JsString::from_utf8(b.to_debug_string());
        if (c.is_string())
          result += c.as_string()->data();
        else
          result += JsString::from_utf8(c.to_debug_string());
        auto *str = gc_.allocate<JsString>(std::move(result));
        reg(instr.a()) = Value::object(str);
      } else {
        reg(instr.a()) = Value::number(b.as_number() + c.as_number());
      }
      break;
    }

    case OpCode::Sub:
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() -
                                     reg(instr.c()).as_number());
      break;

    case OpCode::Mul:
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() *
                                     reg(instr.c()).as_number());
      break;

    case OpCode::Div:
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() /
                                     reg(instr.c()).as_number());
      break;

    case OpCode::Mod:
      reg(instr.a()) = Value::number(
          std::fmod(reg(instr.b()).as_number(), reg(instr.c()).as_number()));
      break;

    case OpCode::Pow:
      reg(instr.a()) = Value::number(
          std::pow(reg(instr.b()).as_number(), reg(instr.c()).as_number()));
      break;

    case OpCode::Neg:
      reg(instr.a()) = Value::number(-reg(instr.b()).as_number());
      break;

      // --- Bitwise ---

    case OpCode::BitAnd:
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) &
          static_cast<int32_t>(reg(instr.c()).as_number())));
      break;

    case OpCode::BitOr:
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) |
          static_cast<int32_t>(reg(instr.c()).as_number())));
      break;

    case OpCode::BitXor:
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) ^
          static_cast<int32_t>(reg(instr.c()).as_number())));
      break;

    case OpCode::BitNot:
      reg(instr.a()) = Value::number(static_cast<double>(
          ~static_cast<int32_t>(reg(instr.b()).as_number())));
      break;

    case OpCode::ShiftLeft:
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number())
          << (static_cast<uint32_t>(reg(instr.c()).as_number()) & 0x1F)));
      break;

    case OpCode::ShiftRight:
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) >>
          (static_cast<uint32_t>(reg(instr.c()).as_number()) & 0x1F)));
      break;

    case OpCode::ShiftRightU:
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<uint32_t>(reg(instr.b()).as_number()) >>
          (static_cast<uint32_t>(reg(instr.c()).as_number()) & 0x1F)));
      break;

      // --- Comparison ---

    case OpCode::Equal:
      reg(instr.a()) =
          Value::boolean(reg(instr.b()).abstract_equals(reg(instr.c())));
      break;

    case OpCode::NotEqual:
      reg(instr.a()) =
          Value::boolean(!reg(instr.b()).abstract_equals(reg(instr.c())));
      break;

    case OpCode::StrictEqual:
      reg(instr.a()) =
          Value::boolean(reg(instr.b()).strict_equals(reg(instr.c())));
      break;

    case OpCode::StrictNotEqual:
      reg(instr.a()) =
          Value::boolean(!reg(instr.b()).strict_equals(reg(instr.c())));
      break;

    case OpCode::LessThan:
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() <
                                      reg(instr.c()).as_number());
      break;

    case OpCode::LessEqual:
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() <=
                                      reg(instr.c()).as_number());
      break;

    case OpCode::GreaterThan:
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() >
                                      reg(instr.c()).as_number());
      break;

    case OpCode::GreaterEqual:
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() >=
                                      reg(instr.c()).as_number());
      break;

      // --- Logical / unary ---

    case OpCode::Not:
      reg(instr.a()) = Value::boolean(!reg(instr.b()).is_truthy());
      break;

    case OpCode::TypeOf: {
      std::string type_str = reg(instr.b()).type_of();
      auto *str = gc_.allocate<JsString>(JsString::from_utf8(type_str));
      reg(instr.a()) = Value::object(str);
      break;
    }

      // --- Global variables ---

    case OpCode::GetGlobal: {
      const Value &name_val = cf.function->constants[instr.bx()];
      std::string name = name_val.as_string()->to_utf8();
      auto it = globals_.find(name);
      if (it != globals_.end()) {
        reg(instr.a()) = it->second;
      } else {
        reg(instr.a()) = Value::undefined();
      }
      break;
    }

    case OpCode::SetGlobal: {
      const Value &name_val = cf.function->constants[instr.bx()];
      std::string name = name_val.as_string()->to_utf8();
      globals_[name] = reg(instr.a());
      break;
    }

      // --- Functions ---

    case OpCode::Closure: {
      // Closure A, Bx — create JsFunction from child prototype functions[Bx]
      uint16_t func_idx = instr.bx();
      BytecodeFunction *proto = &cf.function->functions[func_idx];
      auto *fn = gc_.allocate<JsFunction>(proto);
      reg(instr.a()) = Value::object(fn);
      break;
    }

    case OpCode::Call: {
      // Call A, B, C — callee in R[A], B args in R[A+1..A+B]
      // Return value will be stored in R[A] after the call returns
      Value callee_val = reg(instr.a());
      if (!callee_val.is_function()) {
        std::cerr << "Runtime error: attempting to call a non-function value\n";
        return InterpretResult::RuntimeError;
      }
      if (call_stack_.size() >= kMaxCallDepth) {
        std::cerr << "Runtime error: maximum call stack depth exceeded\n";
        return InterpretResult::RuntimeError;
      }
      JsFunction *fn = callee_val.as_function();
      CallFrame new_frame;
      new_frame.function = fn->prototype();
      new_frame.ip = 0;
      // Callee's registers start right after the callee slot in the caller's
      // window
      new_frame.base_register = cf.base_register + instr.a() + 1;
      call_stack_.push_back(new_frame);
      break;
    }

      // --- Built-ins ---

    case OpCode::Print: {
      uint8_t start = instr.a();
      uint8_t count = instr.b();
      for (uint8_t i = 0; i < count; ++i) {
        if (i > 0)
          out_ << " ";
        out_ << reg(start + i).to_print_string();
      }
      out_ << "\n";
      break;
    }

      // --- Control flow ---

    case OpCode::Jump:
      cf.ip += instr.sbx();
      break;

    case OpCode::JumpIfTrue:
      if (reg(instr.a()).is_truthy())
        cf.ip += instr.sbx();
      break;

    case OpCode::JumpIfFalse:
      if (!reg(instr.a()).is_truthy())
        cf.ip += instr.sbx();
      break;

    case OpCode::Return: {
      Value return_val = reg(instr.a());
      uint16_t callee_base = cf.base_register;
      call_stack_.pop_back();
      if (call_stack_.empty())
        return InterpretResult::Ok;
      registers_[callee_base - 1] = return_val;
      break;
    }

    case OpCode::ReturnUndef: {
      uint16_t callee_base = cf.base_register;
      call_stack_.pop_back();
      if (call_stack_.empty())
        return InterpretResult::Ok;
      // Store undefined in the caller's result slot (one before callee's base)
      registers_[callee_base - 1] = Value::undefined();
      break;
    }

    default:
      std::cerr << "VM error: unhandled opcode " << opcode_name(instr.opcode())
                << " at ip=" << (cf.ip - 1) << "\n";
      return InterpretResult::RuntimeError;
    }
  }

  // Fell off the end of the code — implicit return
  call_stack_.pop_back();
  return InterpretResult::Ok;
}

Value &VM::reg(uint8_t index) {
  return registers_[current_frame().base_register + index];
}

const Value &VM::get_register(uint8_t index) const { return registers_[index]; }

CallFrame &VM::current_frame() { return call_stack_.back(); }

void VM::collect_garbage() {
  mark_roots();
  gc_.collect();
}

void VM::mark_roots() {
  // Mark registers in use across all active frames
  if (!call_stack_.empty()) {
    // High-water mark: top frame's base + its function's register count
    auto &top = call_stack_.back();
    size_t high = top.base_register + top.function->register_count;
    for (size_t i = 0; i < high; ++i) {
      gc_.mark_value(registers_[i]);
    }
  }

  // Mark all global variables
  for (auto &[name, val] : globals_) {
    gc_.mark_value(val);
  }

  // Mark constants in all active call frames
  for (auto &frame : call_stack_) {
    for (auto &val : frame.function->constants) {
      gc_.mark_value(val);
    }
  }
}

} // namespace yatsi
