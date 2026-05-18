#include "typechecker/types.h"

namespace yatsi {

std::string type_to_string(const Type& t) {
  return std::visit(
      [](const auto& inner) -> std::string {
        using T = std::decay_t<decltype(inner)>;
        if constexpr (std::is_same_v<T, NumberType>) {
          return "number";
        } else if constexpr (std::is_same_v<T, StringType>) {
          return "string";
        } else if constexpr (std::is_same_v<T, BooleanType>) {
          return "boolean";
        } else if constexpr (std::is_same_v<T, NullType>) {
          return "null";
        } else if constexpr (std::is_same_v<T, UndefinedType>) {
          return "undefined";
        } else if constexpr (std::is_same_v<T, VoidType>) {
          return "void";
        } else if constexpr (std::is_same_v<T, AnyType>) {
          return "any";
        } else if constexpr (std::is_same_v<T, FunctionType>) {
          std::string result = "(";
          for (size_t i = 0; i < inner.param_types.size(); ++i) {
            if (i > 0) result += ", ";
            result += type_to_string(*inner.param_types[i]);
          }
          result += ") => ";
          result += type_to_string(*inner.return_type);
          return result;
        } else if constexpr (std::is_same_v<T, UnionType>) {
          std::string result;
          for (size_t i = 0; i < inner.members.size(); ++i) {
            if (i > 0) result += " | ";
            result += type_to_string(*inner.members[i]);
          }
          return result;
        }
      },
      static_cast<const Type::variant&>(t));
}

} // namespace yatsi