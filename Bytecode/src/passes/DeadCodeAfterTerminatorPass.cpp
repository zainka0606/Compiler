#include "PassFactories.h"
#include "PassUtils.h"

#include <memory>
#include <string_view>
#include <unordered_set>

namespace compiler::bytecode {

namespace {

class DeadCodeAfterTerminatorPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "DeadCodeAfterTerminatorPass";
    }

    bool Run(Function &function) override {
        if (function.code.empty()) {
            return false;
        }

        std::unordered_set<Address> leaders;
        leaders.insert(function.entry_pc);
        for (Address pc = 0; pc < function.code.size(); ++pc) {
            const Instruction &inst = function.code[pc];
            if (IsJumpOpcode(inst.opcode) && inst.target < function.code.size()) {
                leaders.insert(inst.target);
            }
        }

        bool changed = false;
        bool dead = false;
        for (Address pc = 0; pc < function.code.size(); ++pc) {
            if (leaders.contains(pc)) {
                dead = false;
            }

            Instruction &inst = function.code[pc];
            if (dead && inst.opcode != OpCode::Nop) {
                inst.opcode = OpCode::Nop;
                changed = true;
                continue;
            }

            if (inst.opcode == OpCode::Jump || inst.opcode == OpCode::Return) {
                dead = true;
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateDeadCodeAfterTerminatorPass() {
    return std::make_unique<DeadCodeAfterTerminatorPass>();
}

} // namespace compiler::bytecode
