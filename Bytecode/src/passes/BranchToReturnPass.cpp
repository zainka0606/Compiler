#include "PassFactories.h"
#include "PassUtils.h"

#include <memory>
#include <string_view>

namespace compiler::bytecode {

namespace {

class BranchToReturnPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "BranchToReturnPass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        for (Address pc = 0; pc + 1 < function.code.size(); ++pc) {
            Instruction &inst = function.code[pc];
            if (!IsConditionalJumpOpcode(inst.opcode) ||
                inst.target >= function.code.size()) {
                continue;
            }

            const Instruction &fallthrough = function.code[pc + 1];
            const Instruction &taken = function.code[inst.target];
            if (fallthrough.opcode != OpCode::Return ||
                taken.opcode != OpCode::Return) {
                continue;
            }

            inst = Instruction{};
            inst.opcode = OpCode::Return;
            inst.a = Operand::Null();
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateBranchToReturnPass() {
    return std::make_unique<BranchToReturnPass>();
}

} // namespace compiler::bytecode
