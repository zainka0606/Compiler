#include "PassFactories.h"

namespace compiler::ir {

namespace {

class ControlFlowCleanupPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "ControlFlowCleanupPass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            changed |= RunOnFunction(function);
        }
        return changed;
    }

  private:
    static bool RunOnFunction(Function &function) {
        bool changed = false;
        for (BasicBlock &block : function.blocks) {
            if (!block.terminator.has_value()) {
                continue;
            }
            Terminator &term = *block.terminator;
            if (auto *branch = std::get_if<BranchTerm>(&term)) {
                if (branch->true_target == branch->false_target) {
                    term = JumpTerm{.target = branch->true_target};
                    changed = true;
                }
            }

            auto *jump = std::get_if<JumpTerm>(&term);
            if (jump == nullptr || jump->target >= function.blocks.size()) {
                continue;
            }
            BasicBlock &target = function.blocks[jump->target];
            if (!target.instructions.empty() ||
                !target.terminator.has_value()) {
                continue;
            }
            const auto *ret = std::get_if<ReturnTerm>(&*target.terminator);
            if (ret == nullptr) {
                continue;
            }
            term = *ret;
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateControlFlowCleanupPass() {
    return std::make_unique<ControlFlowCleanupPass>();
}

} // namespace compiler::ir
