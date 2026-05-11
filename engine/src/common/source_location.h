#pragma once

#include <string>

namespace yatsi {

// Represents a location in the source code, used for error reporting and debugging.
struct SourceLocation {
  std::string file;
  int line = 1;
  int column = 1;
};

} // namespace yatsi
