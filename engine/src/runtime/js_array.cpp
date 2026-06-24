#include "runtime/js_array.h"
#include "runtime/gc.h"
#include "runtime/heap_object.h"

namespace yatsi {
JsArray::JsArray() : HeapObject(HeapObjectKind::Array) {}

// Setting in the array is just a wrapper around setting in the std::vector,
// with one caveat:
// if we set something outside the bounds of the array,
// we should grow the vector to handle it, and fill with undefined
void JsArray::set(size_t index, Value val) {
  if (index >= items_.size()) {
    items_.resize(index + 1, Value::undefined());
  }
  items_[index] = val;
}

// Getting requires a quick bounds check.
// If the index is in range, we return the item there in the out parameter,
// and then return true
//
// If the index is out of range, we return false so the VM can return
// and undefined value in the JS runtime
bool JsArray::get(size_t index, Value &out) const {
  if (index < items_.size()) {
    out = items_[index];
    return true;
  }
  return false;
}

// Length is also a simple wrapper around std::vector::size
size_t JsArray::length() const { return items_.size(); }
} // namespace yatsi