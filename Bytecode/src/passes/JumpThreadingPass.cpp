#include "PassFactories.h"
#include "PassUtils.h"

#include <memory>
#include <string_view>
#include <unordered_set>

namespace compiler::bytecode {

namespace {

class JumpThreadingPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "JumpThreadingPass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        if (function.code.empty()) {
            return false;
        }

        for (Instruction &inst : function.code) {
            if (!IsJumpOpcode(inst.opcode) || inst.target >= function.code.size()) {
                continue;
            }
            Address target = inst.target;
            std::unordered_set<Address> seen;
            while (target < function.code.size() &&
                   function.code[target].opcode == OpCode::Jump) {
                if (!seen.insert(target).second) {
                    break;
                }
                target = function.code[target].target;
            }
            if (target != inst.target) {
                inst.target = target;
                changed = true;
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateJumpThreadingPass() {
    return std::make_unique<JumpThreadingPass>();
}

} // namespace compiler::bytecode
