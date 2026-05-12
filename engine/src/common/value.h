#pragma once

#include <cassert>
#include <cstdint>
#include <string>

namespace yatsi {

class HeapObject;

enum class ValueKind : uint8_t {
  Number,
  Boolean,
  Null,
  Undefined,
  Object,
};

class Value {
public:
  // Factory methods
  static Value number(double n);
  static Value boolean(bool b);
  static Value null();
  static Value undefined();
  static Value object(HeapObject* obj);

  // Type queries
  bool is_number() const { return kind_ == ValueKind::Number; }
  bool is_boolean() const { return kind_ == ValueKind::Boolean; }
  bool is_null() const { return kind_ == ValueKind::Null; }
  bool is_undefined() const { return kind_ == ValueKind::Undefined; }
  bool is_object() const { return kind_ == ValueKind::Object; }

  // Unwrapping (asserts correct kind)
  double as_number() const;
  bool as_boolean() const;
  HeapObject* as_object() const;

  // String convenience (object kind must be String)
  bool is_string() const;
  class JsString* as_string() const;

  // Function convenience 
  bool is_function() const;
  class JsFunction* as_function() const;

  ValueKind kind() const { return kind_; }

  // JS semantics
  bool is_truthy() const;
  bool strict_equals(const Value& other) const;
  bool abstract_equals(const Value& other) const; // JS == with type coercion
  std::string type_of() const; // "number", "string", "boolean", etc.

  // Debug representation for disassembler output
  std::string to_debug_string() const;

  // Console output (like JS console.log — strings without quotes)
  std::string to_print_string() const;

private:
  ValueKind kind_;
  union {
    double number_;
    bool boolean_;
    HeapObject* object_;
  };
};

static_assert(sizeof(Value) <= 16);

} // namespace yatsi
