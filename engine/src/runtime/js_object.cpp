#include "runtime/js_object.h"
#include "runtime/gc.h"
#include "runtime/heap_object.h"

namespace yatsi {
JsObject::JsObject() : HeapObject(HeapObjectKind::Object) {}

// First we look up the key property_index_. If it exists,
// we update the value in place. If not, we push a new pair and record the index
void JsObject::set(const std::string &key, Value val) {
  // Create an iterator for the property_index_, calling find on `key`
  auto it = property_index_.find(key);
  // If the iterator finds something before the end,
  // we access our properties_ vector at that index and set its second field to
  // the value provided here.
  if (it != property_index_.end()) {
    properties_[it->second].second = val;
  } else {
    // Otherwise, we store the current `properties_.size()` value in
    // `property_index_` for this key, and we push a new property onto
    // properties_ by constructing one with key/value pair,
    property_index_[key] = properties_.size();
    properties_.push_back({key, val});
  }
}

bool JsObject::get(const std::string &key, Value &out) const {
  // Check if the property exists.
  auto it = property_index_.find(key);
  if (it != property_index_.end()) {
    // If we found the property in the index,
    // set out to that value and return true
    out = properties_[it->second].second;
    return true;
  }

  // If the proeprty doesn't exist, return false
  return false;
}

bool JsObject::has(const std::string &key) const {
  // Check if the property exists
  return property_index_.count(key) > 0;
}

// Tell the garbage collector about every Value we hold inside this object,
// so loop through all properties and mark each value
void JsObject::trace(GarbageCollector &gc) {
  for (auto &[key, val] : properties_) {
    gc.mark_value(val);
  }
}

} // namespace yatsi