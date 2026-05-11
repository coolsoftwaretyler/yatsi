#pragma once

#include <cstdint>

namespace yatsi {

enum class HeapObjectKind : uint8_t {
  String,
  Object,
  Function,
  Array,
};

class GarbageCollector;

class HeapObject {
public:
  HeapObjectKind heap_kind() const { return heap_kind_; }

  bool gc_marked() const { return gc_marked_; }
  void set_gc_marked(bool m) { gc_marked_ = m; }

  HeapObject* gc_next() const { return gc_next_; }
  void set_gc_next(HeapObject* next) { gc_next_ = next; }

  virtual void trace(GarbageCollector& gc) = 0;
  virtual ~HeapObject() = default;

protected:
  explicit HeapObject(HeapObjectKind kind) : heap_kind_(kind) {}

  HeapObjectKind heap_kind_;
  bool gc_marked_ = false;
  HeapObject* gc_next_ = nullptr;
};

} // namespace yatsi
