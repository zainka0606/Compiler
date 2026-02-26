#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace compiler::common {
std::string ReadTextFile(const std::filesystem::path &path);
void WriteTextFile(const std::filesystem::path &path, std::string_view text);
} // namespace compiler::common
