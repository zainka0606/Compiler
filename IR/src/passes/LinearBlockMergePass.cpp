#include "PassFactories.h"

#include <type_traits>
#include <utility>
#include <vector>

namespace compiler::ir {

namespace {

class LinearBlockMergePass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "LinearBlockMergePass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            changed |= RunOnFunction(function);
        }
        return changed;
    }

  private:
    static std::vector<std::size_t> BuildPredecessorCounts(
        const Function &function) {
        std::vector<std::size_t> predecessor_counts(function.blocks.size(), 0);
        for (const BasicBlock &block : function.blocks) {
            if (!block.terminator.has_value()) {
                continue;
            }
            std::visit(
                [&](const auto &term) {
                    using T = std::decay_t<decltype(term)>;
                    if constexpr (std::is_same_v<T, JumpTerm>) {
                        if (term.target < predecessor_counts.size()) {
                            ++predecessor_counts[term.target];
                        }
                    } else if constexpr (std::is_same_v<T, BranchTerm>) {
                        if (term.true_target < predecessor_counts.size()) {
                            ++predecessor_counts[term.true_target];
                        }
                        if (term.false_target < predecessor_counts.size()) {
                            ++predecessor_counts[term.false_target];
                        }
                    }
                },
                *block.terminator);
        }
        return predecessor_counts;
    }

    static bool RunOnFunction(Function &function) {
        bool changed = false;
        bool merged = true;
        while (merged) {
            merged = false;
            if (function.blocks.empty()) {
                break;
            }
            const std::vector<std::size_t> predecessor_counts =
                BuildPredecessorCounts(function);

            for (BlockId block_id = 0; block_id < function.blocks.size();
                 ++block_id) {
                BasicBlock &block = function.blocks[block_id];
                if (!block.terminator.has_value()) {
                    continue;
                }
                auto *jump = std::get_if<JumpTerm>(&*block.terminator);
                if (jump == nullptr || jump->target >= function.blocks.size() ||
                    jump->target == block_id || jump->target == function.entry) {
                    continue;
                }
                const BlockId target_id = jump->target;
                if (predecessor_counts[target_id] != 1) {
                    continue;
                }

                BasicBlock &target = function.blocks[target_id];
                if (!target.terminator.has_value()) {
                    continue;
                }

                if (block.label.empty() && !target.label.empty()) {
                    block.label = target.label;
                }

                block.instructions.insert(
                    block.instructions.end(),
                    std::make_move_iterator(target.instructions.begin()),
                    std::make_move_iterator(target.instructions.end()));
                target.instructions.clear();

                block.terminator = std::move(target.terminator);
                target.terminator.reset();
                target.label.clear();

                merged = true;
                changed = true;
                break;
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateLinearBlockMergePass() {
    return std::make_unique<LinearBlockMergePass>();
}

} // namespace compiler::ir
