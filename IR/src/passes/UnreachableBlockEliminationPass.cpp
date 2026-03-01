#include "PassFactories.h"

#include <vector>

namespace compiler::ir {

namespace {

class UnreachableBlockEliminationPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "UnreachableBlockEliminationPass";
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
        if (function.blocks.empty()) {
            return false;
        }

        std::vector reachable(function.blocks.size(), false);
        std::vector<BlockId> work;
        work.push_back(function.entry);

        while (!work.empty()) {
            const BlockId id = work.back();
            work.pop_back();
            if (id >= function.blocks.size() || reachable[id]) {
                continue;
            }
            reachable[id] = true;

            const BasicBlock &block = function.blocks[id];
            if (!block.terminator.has_value()) {
                continue;
            }
            std::visit(
                [&](const auto &term) {
                    using T = std::decay_t<decltype(term)>;
                    if constexpr (std::is_same_v<T, JumpTerm>) {
                        work.push_back(term.target);
                    } else if constexpr (std::is_same_v<T, BranchTerm>) {
                        work.push_back(term.true_target);
                        work.push_back(term.false_target);
                    }
                },
                *block.terminator);
        }

        std::vector remap(function.blocks.size(),
                                   static_cast<BlockId>(-1));
        std::vector<BasicBlock> compact;
        compact.reserve(function.blocks.size());

        for (BlockId old_id = 0; old_id < function.blocks.size(); ++old_id) {
            if (!reachable[old_id]) {
                continue;
            }
            remap[old_id] = static_cast<BlockId>(compact.size());
            BasicBlock moved = std::move(function.blocks[old_id]);
            moved.id = remap[old_id];
            compact.push_back(std::move(moved));
        }

        for (BasicBlock &block : compact) {
            if (!block.terminator.has_value()) {
                continue;
            }
            std::visit(
                [&](auto &term) {
                    using T = std::decay_t<decltype(term)>;
                    if constexpr (std::is_same_v<T, JumpTerm>) {
                        term.target = remap[term.target];
                    } else if constexpr (std::is_same_v<T, BranchTerm>) {
                        term.true_target = remap[term.true_target];
                        term.false_target = remap[term.false_target];
                    }
                },
                *block.terminator);
        }

        const bool changed = compact.size() != function.blocks.size();
        function.entry = remap[function.entry];
        function.blocks = std::move(compact);
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateUnreachableBlockEliminationPass() {
    return std::make_unique<UnreachableBlockEliminationPass>();
}

} // namespace compiler::ir
