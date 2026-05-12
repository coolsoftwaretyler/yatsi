#include "runtime/js_function.h"

namespace yatsi {

JsFunction::JsFunction(BytecodeFunction *prototype)
    : HeapObject(HeapObjectKind::Function), prototype_(prototype) {}

void JsFunction::trace(GarbageCollector & /*gc*/) {
  // No heap children to mark yet, we will do upvalues and calusers later on 
}

} // namespace yatsi