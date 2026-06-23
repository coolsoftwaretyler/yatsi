#pragma once

#include "common/value.h"
#include "runtime/heap_object.h"

#include <unordered_map>
#include <vector>

namespace yatsi {

class JsObject : public HeapObject {
public:
  JsObject();

  void set(const std::string &key, Value val);
  bool get(const std::string &key, Value &out) const;
  bool has(const std::string &key) const;
  const std::vector<std::pair<std::string, Value>> &properties() const { return properties_; }
  void trace(GarbageCollector &gc) override;

private:
  std::vector<std::pair<std::string, Value>> properties_;
  std::unordered_map<std::string, size_t> property_index_;
};

} // namespace yatsi