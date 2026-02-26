#pragma once

#include <string>
#include <string_view>

namespace compiler::common {
std::string EscapeForCppString(std::string_view text);
} // namespace compiler::common
