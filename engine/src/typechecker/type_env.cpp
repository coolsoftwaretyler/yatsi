#include "typechecker/type_env.h"

namespace yatsi {

void TypeEnvironment::define(const std::string &name, const Type &type) {
  bindings_[name] = type;
}

std::optional<Type> TypeEnvironment::lookup(const std::string &name) const {
  auto it = bindings_.find(name);
  if (it != bindings_.end()) {
    return it->second;
  }
  if (parent_) {
    return parent_->lookup(name);
  }
  return std::nullopt;
}

} // namespace yatsi