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

void VM::enable_tracing(std::vector<VMStep> &trace) { trace_ = &trace; }

VMStep VM::make_step(VMStep::Type type, const Instruction &instr) {
  VMStep step;
  step.type = type;
  step.opcode_name = opcode_name(instr.opcode());
  step.a = instr.a();
  step.b = instr.b();
  step.c = instr.c();
  step.bx = instr.bx();
  step.sbx = instr.sbx();
  auto &cf = current_frame();
  step.ip = cf.ip - 1; // ip was already incremented
  step.function_name = cf.function->name;
  step.call_depth = static_cast<int>(call_stack_.size());
  step.base_register = cf.base_register;
  return step;
}

void VM::record_reg_write(VMStep &step, uint8_t reg_idx) {
  step.reg_writes.push_back({reg_idx, reg(reg_idx).to_debug_string()});
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

    case OpCode::LoadConst: {
      reg(instr.a()) = cf.function->constants[instr.bx()];
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = K[" +
                           std::to_string(instr.bx()) + "] (" +
                           reg(instr.a()).to_debug_string() + ")";
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::LoadNull: {
      reg(instr.a()) = Value::null();
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = null";
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::LoadUndef: {
      reg(instr.a()) = Value::undefined();
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = undefined";
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::LoadTrue: {
      reg(instr.a()) = Value::boolean(true);
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = true";
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::LoadFalse: {
      reg(instr.a()) = Value::boolean(false);
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = false";
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::Move: {
      reg(instr.a()) = reg(instr.b());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] (" +
                           reg(instr.a()).to_debug_string() + ")";
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

      // --- Arithmetic (generic) ---

    case OpCode::Add: {
      Value &b = reg(instr.b());
      Value &c = reg(instr.c());
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
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] + R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::Sub: {
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() -
                                     reg(instr.c()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] - R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::Mul: {
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() *
                                     reg(instr.c()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] * R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::Div: {
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() /
                                     reg(instr.c()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] / R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::Mod: {
      reg(instr.a()) = Value::number(
          std::fmod(reg(instr.b()).as_number(), reg(instr.c()).as_number()));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] % R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::Pow: {
      reg(instr.a()) = Value::number(
          std::pow(reg(instr.b()).as_number(), reg(instr.c()).as_number()));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] ** R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::Neg: {
      reg(instr.a()) = Value::number(-reg(instr.b()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = -R[" +
                           std::to_string(instr.b()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::AddNum:
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() +
                                     reg(instr.c()).as_number());
      break;

    case OpCode::SubNum:
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() -
                                     reg(instr.c()).as_number());
      break;

    case OpCode::MulNum:
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() *
                                     reg(instr.c()).as_number());
      break;

    case OpCode::DivNum:
      reg(instr.a()) = Value::number(reg(instr.b()).as_number() /
                                     reg(instr.c()).as_number());
      break;

    case OpCode::ModNum:
      reg(instr.a()) = Value::number(
          std::fmod(reg(instr.b()).as_number(), reg(instr.c()).as_number()));
      break;

    case OpCode::PowNum:
      reg(instr.a()) = Value::number(
          std::pow(reg(instr.b()).as_number(), reg(instr.c()).as_number()));
      break;

    case OpCode::NegNum:
      reg(instr.a()) = Value::number(-reg(instr.b()).as_number());
      break;

      // --- Bitwise ---

    case OpCode::BitAnd: {
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) &
          static_cast<int32_t>(reg(instr.c()).as_number())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] & R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::BitOr: {
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) |
          static_cast<int32_t>(reg(instr.c()).as_number())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] | R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::BitXor: {
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) ^
          static_cast<int32_t>(reg(instr.c()).as_number())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] ^ R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::BitNot: {
      reg(instr.a()) = Value::number(static_cast<double>(
          ~static_cast<int32_t>(reg(instr.b()).as_number())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = ~R[" +
                           std::to_string(instr.b()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::ShiftLeft: {
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number())
          << (static_cast<uint32_t>(reg(instr.c()).as_number()) & 0x1F)));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] << R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::ShiftRight: {
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<int32_t>(reg(instr.b()).as_number()) >>
          (static_cast<uint32_t>(reg(instr.c()).as_number()) & 0x1F)));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] >> R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::ShiftRightU: {
      reg(instr.a()) = Value::number(static_cast<double>(
          static_cast<uint32_t>(reg(instr.b()).as_number()) >>
          (static_cast<uint32_t>(reg(instr.c()).as_number()) & 0x1F)));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = R[" +
                           std::to_string(instr.b()) + "] >>> R[" +
                           std::to_string(instr.c()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

      // --- Comparison ---

    case OpCode::Equal: {
      reg(instr.a()) =
          Value::boolean(reg(instr.b()).abstract_equals(reg(instr.c())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] == R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::NotEqual: {
      reg(instr.a()) =
          Value::boolean(!reg(instr.b()).abstract_equals(reg(instr.c())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] != R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::StrictEqual: {
      reg(instr.a()) =
          Value::boolean(reg(instr.b()).strict_equals(reg(instr.c())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] === R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::StrictNotEqual: {
      reg(instr.a()) =
          Value::boolean(!reg(instr.b()).strict_equals(reg(instr.c())));
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] !== R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::LessThan: {
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() <
                                      reg(instr.c()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] < R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::LessEqual: {
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() <=
                                      reg(instr.c()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] <= R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::GreaterThan: {
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() >
                                      reg(instr.c()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] > R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::GreaterEqual: {
      reg(instr.a()) = Value::boolean(reg(instr.b()).as_number() >=
                                      reg(instr.c()).as_number());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = (R[" +
                           std::to_string(instr.b()) + "] >= R[" +
                           std::to_string(instr.c()) +
                           "]) = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

      // --- Logical / unary ---

    case OpCode::Not: {
      reg(instr.a()) = Value::boolean(!reg(instr.b()).is_truthy());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = !R[" +
                           std::to_string(instr.b()) +
                           "] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::TypeOf: {
      std::string type_str = reg(instr.b()).type_of();
      auto *str = gc_.allocate<JsString>(JsString::from_utf8(type_str));
      reg(instr.a()) = Value::object(str);
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = typeof R[" +
                           std::to_string(instr.b()) + "] = " + type_str;
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
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
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "R[" + std::to_string(instr.a()) + "] = globals[\"" +
                           name + "\"] = " + reg(instr.a()).to_debug_string();
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::SetGlobal: {
      const Value &name_val = cf.function->constants[instr.bx()];
      std::string name = name_val.as_string()->to_utf8();
      globals_[name] = reg(instr.a());
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "globals[\"" + name + "\"] = R[" +
                           std::to_string(instr.a()) + "] (" +
                           reg(instr.a()).to_debug_string() + ")";
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::GetUpvalue: {
      uint8_t uv_idx = instr.b();
      Upvalue *uv = cf.closure->upvalues[uv_idx];
      reg(instr.a()) = *uv->location;
      if (trace_) {
        auto step = make_step(VMStep::Type::ReadUpvalue, instr);
        step.upvalue_index = uv_idx;
        step.upvalue_is_open = uv->is_open;
        step.upvalue_value = uv->location->to_debug_string();
        // Try to find variable name from upvalue descriptors
        if (cf.closure && uv_idx < cf.function->upvalue_descs.size()) {
          // Walk compiler trace if available — for now use index
        }
        step.description = "R[" + std::to_string(instr.a()) + "] = upvalues[" +
                           std::to_string(uv_idx) +
                           "] = " + step.upvalue_value +
                           (uv->is_open ? " (open)" : " (closed)");
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::SetUpvalue: {
      uint8_t uv_idx = instr.b();
      Upvalue *uv = cf.closure->upvalues[uv_idx];
      *uv->location = reg(instr.a());
      if (trace_) {
        auto step = make_step(VMStep::Type::WriteUpvalue, instr);
        step.upvalue_index = uv_idx;
        step.upvalue_is_open = uv->is_open;
        step.upvalue_value = uv->location->to_debug_string();
        step.description = "upvalues[" + std::to_string(uv_idx) + "] = R[" +
                           std::to_string(instr.a()) + "] (" +
                           step.upvalue_value + ")" +
                           (uv->is_open ? " (open)" : " (closed)");
        trace_->push_back(std::move(step));
      }
      break;
    }

    case OpCode::CloseUpvalue: {
      if (trace_) {
        // Record which upvalues will be closed before actually closing them
        auto step = make_step(VMStep::Type::CloseUpvalue, instr);
        uint16_t from_reg = cf.base_register + instr.a();
        std::string closed_list;
        int count = 0;
        for (auto *uv = open_upvalues_; uv && uv->register_index >= from_reg;
             uv = uv->next_open) {
          if (count > 0)
            closed_list += ", ";
          closed_list += "reg" + std::to_string(uv->register_index) + "=" +
                         uv->location->to_debug_string();
          count++;
        }
        step.description = "Close upvalues from R[" +
                           std::to_string(instr.a()) + "] (abs " +
                           std::to_string(from_reg) +
                           "): " + (count > 0 ? closed_list : "none");
        close_upvalues(from_reg);
        trace_->push_back(std::move(step));
      } else {
        close_upvalues(cf.base_register + instr.a());
      }
      break;
    }

      // --- Functions ---

    case OpCode::Closure: {
      uint16_t func_idx = instr.bx();
      BytecodeFunction *proto = &cf.function->functions[func_idx];
      auto *fn = gc_.allocate<JsFunction>(proto);

      for (const auto &desc : proto->upvalue_descs) {
        if (desc.is_local) {
          uint16_t abs_reg = cf.base_register + desc.index;
          fn->upvalues.push_back(capture_upvalue(abs_reg));
        } else {
          fn->upvalues.push_back(cf.closure->upvalues[desc.index]);
        }
      }

      reg(instr.a()) = Value::object(fn);

      if (trace_) {
        // Main Closure step
        auto step = make_step(VMStep::Type::Execute, instr);
        step.closure_func_index = func_idx;
        step.upvalue_count = static_cast<int>(proto->upvalue_descs.size());
        step.description = "R[" + std::to_string(instr.a()) + "] = Closure(" +
                           proto->name + ") with " +
                           std::to_string(proto->upvalue_descs.size()) +
                           " upvalue(s)";
        record_reg_write(step, instr.a());
        trace_->push_back(std::move(step));

        // Emit a CaptureUpvalue step for each upvalue captured
        for (size_t i = 0; i < proto->upvalue_descs.size(); ++i) {
          const auto &desc = proto->upvalue_descs[i];
          VMStep uv_step;
          uv_step.type = VMStep::Type::CaptureUpvalue;
          uv_step.opcode_name = "Closure";
          uv_step.a = instr.a();
          uv_step.bx = instr.bx();
          uv_step.ip = cf.ip - 1;
          uv_step.function_name = cf.function->name;
          uv_step.call_depth = static_cast<int>(call_stack_.size());
          uv_step.base_register = cf.base_register;
          uv_step.upvalue_index = static_cast<int>(i);
          uv_step.closure_func_index = func_idx;

          Upvalue *captured_uv = fn->upvalues[i];
          uv_step.upvalue_is_open = captured_uv->is_open;
          uv_step.upvalue_value = captured_uv->location->to_debug_string();

          if (desc.is_local) {
            uv_step.description =
                "  UV[" + std::to_string(i) + "] captures local R[" +
                std::to_string(desc.index) + "] (abs " +
                std::to_string(cf.base_register + desc.index) +
                ") = " + uv_step.upvalue_value;
          } else {
            uv_step.description =
                "  UV[" + std::to_string(i) + "] chains from enclosing UV[" +
                std::to_string(desc.index) + "] = " + uv_step.upvalue_value;
          }
          trace_->push_back(std::move(uv_step));
        }
      }
      break;
    }

    case OpCode::Call: {
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
      new_frame.closure = fn;
      new_frame.base_register = cf.base_register + instr.a() + 1;

      if (trace_) {
        auto step = make_step(VMStep::Type::Call, instr);
        step.description =
            "Call " + fn->prototype()->name + "(" + std::to_string(instr.b()) +
            " args), base=" + std::to_string(new_frame.base_register);
        trace_->push_back(std::move(step));
      }

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
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        std::string vals;
        for (uint8_t i = 0; i < count; ++i) {
          if (i > 0)
            vals += ", ";
          vals += reg(start + i).to_debug_string();
        }
        step.description =
            "Print " + std::to_string(count) + " value(s): " + vals;
        trace_->push_back(std::move(step));
      }
      break;
    }

      // --- Control flow ---

    case OpCode::Jump: {
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "Jump " + std::to_string(instr.sbx()) +
                           " -> ip=" + std::to_string(cf.ip + instr.sbx());
        trace_->push_back(std::move(step));
      }
      cf.ip += instr.sbx();
      break;
    }

    case OpCode::JumpIfTrue: {
      bool taken = reg(instr.a()).is_truthy();
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "JumpIfTrue R[" + std::to_string(instr.a()) + "] (" +
                           reg(instr.a()).to_debug_string() + ") " +
                           (taken ? "-> taken" : "-> not taken");
        trace_->push_back(std::move(step));
      }
      if (taken)
        cf.ip += instr.sbx();
      break;
    }

    case OpCode::JumpIfFalse: {
      bool taken = !reg(instr.a()).is_truthy();
      if (trace_) {
        auto step = make_step(VMStep::Type::Execute, instr);
        step.description = "JumpIfFalse R[" + std::to_string(instr.a()) +
                           "] (" + reg(instr.a()).to_debug_string() + ") " +
                           (taken ? "-> taken" : "-> not taken");
        trace_->push_back(std::move(step));
      }
      if (taken)
        cf.ip += instr.sbx();
      break;
    }

    case OpCode::Return: {
      Value return_val = reg(instr.a());
      uint16_t callee_base = cf.base_register;
      std::string func_name = cf.function->name;

      if (trace_) {
        // Record close_upvalues info before they get closed
        auto step = make_step(VMStep::Type::Return, instr);
        step.description = "Return from " + func_name + " with " +
                           return_val.to_debug_string();
        trace_->push_back(std::move(step));
      }

      close_upvalues(callee_base);
      call_stack_.pop_back();
      if (call_stack_.empty())
        return InterpretResult::Ok;
      registers_[callee_base - 1] = return_val;
      break;
    }

    case OpCode::ReturnUndef: {
      uint16_t callee_base = cf.base_register;
      std::string func_name = cf.function->name;

      if (trace_) {
        auto step = make_step(VMStep::Type::Return, instr);
        step.description = "Return undefined from " + func_name;
        trace_->push_back(std::move(step));
      }

      close_upvalues(callee_base);
      call_stack_.pop_back();
      if (call_stack_.empty())
        return InterpretResult::Ok;
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

Upvalue *VM::capture_upvalue(uint16_t abs_reg) {
  // Walk the open upvalue list to see if we already have one for this register
  Upvalue *prev = nullptr;
  Upvalue *curr = open_upvalues_;
  while (curr && curr->register_index > abs_reg) {
    prev = curr;
    curr = curr->next_open;
  }
  // Found an existing open upvalue for this register
  if (curr && curr->register_index == abs_reg) {
    return curr;
  }
  // Create a new one pointing to the register
  auto *uv = new Upvalue();
  uv->location = &registers_[abs_reg];
  uv->register_index = abs_reg;
  uv->is_open = true;
  // Insert into the linked list (sorted descending by register_index)
  uv->next_open = curr;
  if (prev) {
    prev->next_open = uv;
  } else {
    open_upvalues_ = uv;
  }
  return uv;
}

void VM::close_upvalues(uint16_t from_reg) {
  while (open_upvalues_ && open_upvalues_->register_index >= from_reg) {
    Upvalue *uv = open_upvalues_;
    uv->closed_value = *uv->location;
    uv->location = &uv->closed_value;
    uv->is_open = false;
    open_upvalues_ = uv->next_open;
    uv->next_open = nullptr;
  }
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

  // Mark open upvalues (closed upvalues are marked via JsFunction::trace)
  for (auto *uv = open_upvalues_; uv; uv = uv->next_open) {
    gc_.mark_value(*uv->location);
  }
}

} // namespace yatsi
