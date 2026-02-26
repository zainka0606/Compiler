#pragma once

#include <string>
#include <string_view>

namespace compiler::common {
std::string EscapeGraphvizLabel(std::string_view text);
} // namespace compiler::common
