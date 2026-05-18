#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>
    
namespace yatsi {

// --- Primitive type tags ---

struct NumberType {};
struct StringType {};
struct BooleanType {};
struct NullType {};
struct UndefinedType {};
struct VoidType {};
struct AnyType {};

// --- Compound types ---

// Forward-declare Type so FunctionType and UnionType can reference it
struct Type;
using TypePtr = std::shared_ptr<Type>;

struct FunctionType {
  std::vector<TypePtr> param_types;
  TypePtr return_type;
};

struct UnionType {
  std::vector<TypePtr> members;
};

// --- Type variant ---

struct Type : std::variant<NumberType, StringType, BooleanType, NullType,
                           UndefinedType, VoidType, AnyType, FunctionType,
                           UnionType> {
  using variant::variant;
};

// --- Convenience predicates ---

inline bool is_number_type(const Type& t) {
  return std::holds_alternative<NumberType>(t);
}

inline bool is_string_type(const Type& t) {
  return std::holds_alternative<StringType>(t);
}

inline bool is_boolean_type(const Type& t) {
  return std::holds_alternative<BooleanType>(t);
}

inline bool is_null_type(const Type& t) {
  return std::holds_alternative<NullType>(t);
}

inline bool is_undefined_type(const Type& t) {
  return std::holds_alternative<UndefinedType>(t);
}

inline bool is_void_type(const Type& t) {
  return std::holds_alternative<VoidType>(t);
}

inline bool is_any_type(const Type& t) {
  return std::holds_alternative<AnyType>(t);
}

inline bool is_function_type(const Type& t) {
  return std::holds_alternative<FunctionType>(t);
}

inline bool is_union_type(const Type& t) {
  return std::holds_alternative<UnionType>(t);
}

// --- Diagnostics ---

std::string type_to_string(const Type& t);

} // namespace yatsi 