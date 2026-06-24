#pragma once

#include "common/value.h"
#include "runtime/heap_object.h"
#include <cstddef>
#include <vector>

namespace yatsi {
class JsArray : public HeapObject {
public:
  JsArray();

  void set(size_t index, Value val);
  bool get(size_t index, Value &out) const;
  size_t length() const;
  void trace(GarbageCollector &gc) override;

private:
  std::vector<Value> items_;
};
} // namespace yatsi