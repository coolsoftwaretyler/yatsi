#pragma once

#include "runtime/heap_object.h"

#include <cstddef>
#include <utility>

namespace yatsi {
class Value;
} // namespace yatsi

namespace yatsi {

class GarbageCollector {
public:
  GarbageCollector() = default;
  ~GarbageCollector();

  // Not copyable or movable
  GarbageCollector(const GarbageCollector&) = delete;
  GarbageCollector& operator=(const GarbageCollector&) = delete;

  template <typename T, typename... Args>
  T* allocate(Args&&... args);

  void mark_object(HeapObject* obj);
  void mark_value(const Value& val);
  void collect();

  size_t object_count() const { return object_count_; }

private:
  HeapObject* all_objects_ = nullptr;
  size_t object_count_ = 0;
};

template <typename T, typename... Args>
T* GarbageCollector::allocate(Args&&... args) {
  static_assert(std::is_base_of_v<HeapObject, T>,
                "Can only allocate HeapObject subclasses");
  T* obj = new T(std::forward<Args>(args)...);
  obj->set_gc_next(all_objects_);
  all_objects_ = obj;
  ++object_count_;
  return obj;
}

} // namespace yatsi
