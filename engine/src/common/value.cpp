#include "common/value.h"

#include "runtime/heap_object.h"
#include "runtime/js_function.h"
#include "runtime/js_object.h"
#include "runtime/js_string.h"

#include <limits>

namespace yatsi {

// --- Factory methods ---

Value Value::number(double n) {
  Value v;
  v.kind_ = ValueKind::Number;
  v.number_ = n;
  return v;
}

Value Value::boolean(bool b) {
  Value v;
  v.kind_ = ValueKind::Boolean;
  v.boolean_ = b;
  return v;
}

Value Value::null() {
  Value v;
  v.kind_ = ValueKind::Null;
  v.object_ = nullptr; // zero the union
  return v;
}

Value Value::undefined() {
  Value v;
  v.kind_ = ValueKind::Undefined;
  v.object_ = nullptr; // zero the union
  return v;
}

Value Value::object(HeapObject *obj) {
  Value v;
  v.kind_ = ValueKind::Object;
  v.object_ = obj;
  return v;
}

// --- Unwrapping ---

double Value::as_number() const {
  assert(is_number() && "Value is not a number");
  return number_;
}

bool Value::as_boolean() const {
  assert(is_boolean() && "Value is not a boolean");
  return boolean_;
}

HeapObject *Value::as_object() const {
  assert(is_object() && "Value is not an object");
  return object_;
}

bool Value::is_string() const {
  return is_object() && object_ &&
         object_->heap_kind() == HeapObjectKind::String;
}

JsString *Value::as_string() const {
  assert(is_string() && "Value is not a string");
  return static_cast<JsString *>(object_);
}

bool Value::is_function() const {
  return is_object() && object_ &&
         object_->heap_kind() == HeapObjectKind::Function;
}

bool Value::is_js_object() const {
  return is_object() && object_ &&
         object_->heap_kind() == HeapObjectKind::Object;
}

JsObject *Value::as_js_object() const {
  assert(is_js_object() && "Value is not an object");
  return static_cast<JsObject *>(object_);
}

JsFunction *Value::as_function() const {
  assert(is_function() && "Value is not a function");
  return static_cast<JsFunction *>(object_);
}

// --- JS truthiness ---

bool Value::is_truthy() const {
  switch (kind_) {
  case ValueKind::Number:
    return number_ != 0.0 && number_ == number_; // NaN is falsy
  case ValueKind::Boolean:
    return boolean_;
  case ValueKind::Null:
  case ValueKind::Undefined:
    return false;
  case ValueKind::Object:
    // Strings are special: empty string is falsy
    if (is_string())
      return !as_string()->data().empty();
    return true; // all other objects are truthy in JS
  }
  return false; // unreachable
}

// --- Strict equality (===) ---

bool Value::strict_equals(const Value &other) const {
  if (kind_ != other.kind_)
    return false;
  switch (kind_) {
  case ValueKind::Number:
    // NaN !== NaN per JS spec
    return number_ == other.number_;
  case ValueKind::Boolean:
    return boolean_ == other.boolean_;
  case ValueKind::Null:
  case ValueKind::Undefined:
    return true; // null === null, undefined === undefined
  case ValueKind::Object:
    return object_ == other.object_; // reference equality
  }
  return false;
}

// --- Abstract equality (==) ---

// Helper: try to convert a Value to a number for coercion
static double to_number(const Value &v) {
  switch (v.kind()) {
  case ValueKind::Number:
    return v.as_number();
  case ValueKind::Boolean:
    return v.as_boolean() ? 1.0 : 0.0;
  case ValueKind::Null:
    return 0.0;
  case ValueKind::Undefined:
    return std::numeric_limits<double>::quiet_NaN();
  case ValueKind::Object:
    if (v.is_string()) {
      // Try to parse string as number
      std::string s = v.as_string()->to_utf8();
      if (s.empty())
        return 0.0;
      try {
        size_t pos = 0;
        double result = std::stod(s, &pos);
        if (pos == s.size())
          return result;
      } catch (...) {
      }
      return std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
  }
  return std::numeric_limits<double>::quiet_NaN();
}

bool Value::abstract_equals(const Value &other) const {
  // Same type: use strict equality
  if (kind_ == other.kind_)
    return strict_equals(other);

  // null == undefined (and vice versa)
  if ((is_null() && other.is_undefined()) ||
      (is_undefined() && other.is_null()))
    return true;

  // If either side is boolean, convert it to number and retry
  if (is_boolean()) {
    Value num = Value::number(to_number(*this));
    return num.abstract_equals(other);
  }
  if (other.is_boolean()) {
    Value num = Value::number(to_number(other));
    return abstract_equals(num);
  }

  // Number == String: convert string to number
  if (is_number() && other.is_string()) {
    return number_ == to_number(other);
  }
  if (is_string() && other.is_number()) {
    return to_number(*this) == other.as_number();
  }

  return false;
}

// --- typeof ---

std::string Value::type_of() const {
  switch (kind_) {
  case ValueKind::Number:
    return "number";
  case ValueKind::Boolean:
    return "boolean";
  case ValueKind::Null:
    return "object"; // JS quirk: typeof null === "object"
  case ValueKind::Undefined:
    return "undefined";
  case ValueKind::Object:
    if (is_string())
      return "string";
    if (is_function())
      return "function";
    return "object";
  }
  return "undefined";
}

// --- Debug string ---

std::string Value::to_debug_string() const {
  switch (kind_) {
  case ValueKind::Number: {
    // Print integers without decimal point
    if (number_ == static_cast<int64_t>(number_) && number_ >= -1e15 &&
        number_ <= 1e15) {
      return std::to_string(static_cast<int64_t>(number_));
    }
    return std::to_string(number_);
  }
  case ValueKind::Boolean:
    return boolean_ ? "true" : "false";
  case ValueKind::Null:
    return "null";
  case ValueKind::Undefined:
    return "undefined";
  case ValueKind::Object:
    if (is_string())
      return "\"" + as_string()->to_utf8() + "\"";
    if (is_function())
      return "<function " + as_function()->prototype()->name + ">";
    return "<object>";
  }
  return "<unknown>";
}

// --- Print string (console.log output) ---

std::string Value::to_print_string() const {
  switch (kind_) {
  case ValueKind::Number: {
    if (number_ == static_cast<int64_t>(number_) && number_ >= -1e15 &&
        number_ <= 1e15) {
      return std::to_string(static_cast<int64_t>(number_));
    }
    return std::to_string(number_);
  }
  case ValueKind::Boolean:
    return boolean_ ? "true" : "false";
  case ValueKind::Null:
    return "null";
  case ValueKind::Undefined:
    return "undefined";
  case ValueKind::Object:
    if (is_string())
      return as_string()->to_utf8(); // no quotes, unlike to_debug_string
    if (is_function())
      return "function " + as_function()->prototype()->name +
             "() { [native code] }";
    return "[object Object]";
  }
  return "undefined";
}

} // namespace yatsi
