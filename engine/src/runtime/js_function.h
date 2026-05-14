#pragma once

#include "compiler/bytecode.h"
#include "runtime/heap_object.h"

namespace yatsi {

struct Upvalue {
  Value *location;
  Value closed_value;
  bool is_open = true;

  Upvalue *next_open = nullptr;
  uint16_t register_index = 0;
};

class JsFunction : public HeapObject {
public:
  explicit JsFunction(BytecodeFunction *prototype);

  BytecodeFunction *prototype() const { return prototype_; }

  std::vector<Upvalue *> upvalues;

  void trace(GarbageCollector &gc) override;

private:
  BytecodeFunction
      *prototype_; // non-owning; lifetime managed by parent BytecodeFunction
};

} // namespace yatsi