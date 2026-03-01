#include "PassFactories.h"

#include "PassUtils.h"

namespace compiler::ir {

namespace {

class BranchSimplificationPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "BranchSimplificationPass";
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
                if (const std::optional<bool> truthy =
                        passes::detail::TryEvaluateImmediateTruthiness(
                            branch->condition)) {
                    const BlockId target =
                        *truthy ? branch->true_target : branch->false_target;
                    term = JumpTerm{.target = target};
                    changed = true;
                }
            }

            std::visit(
                [&](auto &target_term) {
                    using T = std::decay_t<decltype(target_term)>;
                    if constexpr (std::is_same_v<T, JumpTerm>) {
                        const BlockId threaded =
                            passes::detail::ThreadJumpTarget(
                                function, target_term.target);
                        if (threaded != target_term.target) {
                            target_term.target = threaded;
                            changed = true;
                        }
                    } else if constexpr (std::is_same_v<T, BranchTerm>) {
                        const BlockId threaded_true =
                            passes::detail::ThreadJumpTarget(
                                function, target_term.true_target);
                        const BlockId threaded_false =
                            passes::detail::ThreadJumpTarget(
                                function, target_term.false_target);
                        if (threaded_true != target_term.true_target) {
                            target_term.true_target = threaded_true;
                            changed = true;
                        }
                        if (threaded_false != target_term.false_target) {
                            target_term.false_target = threaded_false;
                            changed = true;
                        }
                    }
                },
                term);
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateBranchSimplificationPass() {
    return std::make_unique<BranchSimplificationPass>();
}

} // namespace compiler::ir
