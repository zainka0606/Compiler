#pragma once

#include "AST.h"

#include <stdexcept>
#include <string_view>

namespace compiler::lang {

class ParserException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

AST ParseProgram(std::string_view source_text);

} // namespace compiler::lang
