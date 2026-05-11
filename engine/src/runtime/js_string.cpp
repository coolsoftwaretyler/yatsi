#include "runtime/js_string.h"

namespace yatsi {

JsString::JsString(std::u16string data)
    : HeapObject(HeapObjectKind::String), data_(std::move(data)) {}

std::string JsString::to_utf8() const {
  std::string result;
  result.reserve(data_.size()); // rough estimate
  for (char16_t ch : data_) {
    if (ch < 0x80) {
      result.push_back(static_cast<char>(ch));
    } else if (ch < 0x800) {
      result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
      result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
  }
  return result;
}

std::u16string JsString::from_utf8(const std::string& str) {
  std::u16string result;
  result.reserve(str.size());
  for (char ch : str) {
    result.push_back(static_cast<char16_t>(static_cast<unsigned char>(ch)));
  }
  return result;
}

void JsString::trace(GarbageCollector& /*gc*/) {
  // Strings contain no references to other heap objects
}

} // namespace yatsi
