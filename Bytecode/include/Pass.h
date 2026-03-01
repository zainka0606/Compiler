#pragma once

#include <memory>
#include <string_view>
#include <vector>

namespace compiler::bytecode {

struct Function;

class Pass {
  public:
    virtual ~Pass() = default;

    [[nodiscard]] virtual std::string_view Name() const = 0;
    [[nodiscard]] virtual bool RunOnce() const { return false; }
    virtual bool Run(Function &function) = 0;
};

std::vector<std::unique_ptr<Pass>> BuildDefaultPassPipeline();

} // namespace compiler::bytecode
