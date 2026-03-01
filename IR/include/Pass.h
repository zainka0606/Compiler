#pragma once

#include "IR.h"

#include <string_view>

namespace compiler::ir {

class Pass {
  public:
    virtual ~Pass() = default;

    [[nodiscard]] virtual std::string_view Name() const = 0;
    virtual bool Run(Program &program) = 0;
};

} // namespace compiler::ir
