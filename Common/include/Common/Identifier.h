#pragma once

#include <string>
#include <string_view>

namespace compiler::common {
std::string SanitizeIdentifier(std::string_view text,
                               std::string_view fallback);
} // namespace compiler::common
