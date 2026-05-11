#pragma once

#include "runtime/heap_object.h"

#include <string>

namespace yatsi {

class JsString : public HeapObject {
public:
  explicit JsString(std::u16string data);

  const std::u16string& data() const { return data_; }
  std::string to_utf8() const;
  static std::u16string from_utf8(const std::string& str);

  void trace(GarbageCollector& gc) override;

private:
  std::u16string data_;
};

} // namespace yatsi
