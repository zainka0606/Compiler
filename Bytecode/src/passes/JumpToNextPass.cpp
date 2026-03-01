#include "PassFactories.h"
#include "PassUtils.h"

#include <memory>
#include <string_view>

namespace compiler::bytecode {

namespace {

class JumpToNextPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "JumpToNextPass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        for (Address pc = 0; pc < function.code.size(); ++pc) {
            Instruction &inst = function.code[pc];
            if (!IsJumpOpcode(inst.opcode)) {
                continue;
            }
            if (inst.target != pc + 1) {
                continue;
            }
            inst.opcode = OpCode::Nop;
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateJumpToNextPass() {
    return std::make_unique<JumpToNextPass>();
}

} // namespace compiler::bytecode
