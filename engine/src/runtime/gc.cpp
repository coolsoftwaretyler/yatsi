#include "runtime/gc.h"

#include "common/value.h"

namespace yatsi {

GarbageCollector::~GarbageCollector() {
  HeapObject* obj = all_objects_;
  while (obj) {
    HeapObject* next = obj->gc_next();
    delete obj;
    obj = next;
  }
}

void GarbageCollector::mark_object(HeapObject* obj) {
  if (!obj || obj->gc_marked())
    return;
  obj->set_gc_marked(true);
  obj->trace(*this);
}

void GarbageCollector::mark_value(const Value& val) {
  if (val.is_object()) {
    mark_object(val.as_object());
  }
}

void GarbageCollector::collect() {
  // Sweep: free unmarked objects, reset mark on survivors
  HeapObject* prev = nullptr;
  HeapObject* obj = all_objects_;
  while (obj) {
    if (!obj->gc_marked()) {
      HeapObject* unreached = obj;
      obj = obj->gc_next();
      if (prev) {
        prev->set_gc_next(obj);
      } else {
        all_objects_ = obj;
      }
      delete unreached;
      --object_count_;
    } else {
      obj->set_gc_marked(false);
      prev = obj;
      obj = obj->gc_next();
    }
  }
}

} // namespace yatsi
