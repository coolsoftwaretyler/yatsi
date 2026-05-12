#pragma once

#include "compiler/bytecode.h"
#include "runtime/heap_object.h"

namespace yatsi {

class JsFunction : public HeapObject {
public:
  explicit JsFunction(BytecodeFunction *prototype);

  BytecodeFunction *prototype() const { return prototype_; }

  void trace(GarbageCollector &gc) override;

private:
  BytecodeFunction
      *prototype_; // non-owning; lifetime managed by parent BytecodeFunction
};

} // namespace yatsi