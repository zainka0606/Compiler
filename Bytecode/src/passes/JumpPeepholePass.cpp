#include "PassFactories.h"
#include "PassUtils.h"

#include <memory>
#include <string_view>

namespace compiler::bytecode {

namespace {

class JumpPeepholePass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "JumpPeepholePass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        for (Address pc = 0; pc < function.code.size(); ++pc) {
            Instruction &inst = function.code[pc];

            if (inst.opcode == OpCode::Jump && inst.target < function.code.size() &&
                function.code[inst.target].opcode == OpCode::Return) {
                inst = Instruction{};
                inst.opcode = OpCode::Return;
                inst.a = Operand::Null();
                changed = true;
                continue;
            }

            if (!IsConditionalJumpOpcode(inst.opcode) ||
                pc + 1 >= function.code.size()) {
                continue;
            }
            const Instruction &next = function.code[pc + 1];
            if (next.opcode != OpCode::Jump) {
                continue;
            }
            if (next.target != inst.target) {
                continue;
            }
            inst.opcode = OpCode::Nop;
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateJumpPeepholePass() {
    return std::make_unique<JumpPeepholePass>();
}

} // namespace compiler::bytecode

