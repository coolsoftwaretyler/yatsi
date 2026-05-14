#include "runtime/js_function.h"

#include "runtime/gc.h"

namespace yatsi {

JsFunction::JsFunction(BytecodeFunction *prototype)
    : HeapObject(HeapObjectKind::Function), prototype_(prototype) {}

void JsFunction::trace(GarbageCollector &gc) {
  for (auto *uv : upvalues) {
    if (uv && !uv->is_open) {
      gc.mark_value(uv->closed_value);
    }
  }
}

} // namespace yatsi