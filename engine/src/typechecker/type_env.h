#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "typechecker/types.h"

namespace yatsi {

class TypeEnvironment {
public:
  explicit TypeEnvironment(TypeEnvironment *parent = nullptr)
      : parent_(parent) {}

  // Define a variable's type in the current scope
  void define(const std::string &name, const Type &type);

  // Look up a variable's type, searching up the scope chain
  std::optional<Type> lookup(const std::string &name) const;

  // Get all bindings in the current scope (not parent scopes)
  const std::unordered_map<std::string, Type> &all_bindings() const {
    return bindings_;
  }

private:
  TypeEnvironment *parent_;
  std::unordered_map<std::string, Type> bindings_;
};

} // namespace yatsi