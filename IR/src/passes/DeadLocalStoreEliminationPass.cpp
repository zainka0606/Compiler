#include "PassFactories.h"

namespace compiler::ir {

namespace {

bool IsLoadFromSlot(const Instruction &instruction, const SlotId slot) {
    const auto *load = std::get_if<LoadLocalInst>(&instruction);
    return load != nullptr && load->slot == slot;
}

bool IsStoreToSlot(const Instruction &instruction, const SlotId slot) {
    const auto *store = std::get_if<StoreLocalInst>(&instruction);
    return store != nullptr && store->slot == slot;
}

class DeadLocalStoreEliminationPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "DeadLocalStoreEliminationPass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            for (BasicBlock &block : function.blocks) {
                bool block_changed = false;
                std::vector dead(block.instructions.size(), false);
                for (std::size_t i = 0; i < block.instructions.size(); ++i) {
                    const auto *store =
                        std::get_if<StoreLocalInst>(&block.instructions[i]);
                    if (store == nullptr) {
                        continue;
                    }
                    const SlotId slot = store->slot;
                    for (std::size_t j = i + 1; j < block.instructions.size();
                         ++j) {
                        if (IsLoadFromSlot(block.instructions[j], slot)) {
                            break;
                        }
                        if (IsStoreToSlot(block.instructions[j], slot)) {
                            dead[i] = true;
                            block_changed = true;
                            break;
                        }
                    }
                }

                if (!block_changed) {
                    continue;
                }
                changed = true;
                std::vector<Instruction> kept;
                kept.reserve(block.instructions.size());
                for (std::size_t i = 0; i < block.instructions.size(); ++i) {
                    if (dead[i]) {
                        continue;
                    }
                    kept.push_back(std::move(block.instructions[i]));
                }
                block.instructions = std::move(kept);
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateDeadLocalStoreEliminationPass() {
    return std::make_unique<DeadLocalStoreEliminationPass>();
}

} // namespace compiler::ir
