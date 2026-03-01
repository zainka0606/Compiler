#include "PassFactories.h"

#include "PassUtils.h"

#include <unordered_map>

namespace compiler::ir {

namespace {

class DeadTempEliminationPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "DeadTempEliminationPass";
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
        std::unordered_map<LocalId, std::size_t> uses;
        for (const BasicBlock &block : function.blocks) {
            for (const Instruction &instruction : block.instructions) {
                passes::detail::CollectInstructionUses(instruction, uses);
            }
            if (block.terminator.has_value()) {
                passes::detail::CollectTerminatorUses(*block.terminator, uses);
            }
        }

        bool changed = false;
        for (BasicBlock &block : function.blocks) {
            std::vector<Instruction> kept;
            kept.reserve(block.instructions.size());
            for (Instruction &instruction : block.instructions) {
                const std::optional<LocalId> dst =
                    passes::detail::InstructionDestination(instruction);
                if (dst.has_value() && uses[*dst] == 0 &&
                    passes::detail::IsPureInstruction(instruction)) {
                    changed = true;
                    continue;
                }
                kept.push_back(std::move(instruction));
            }
            block.instructions = std::move(kept);
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateDeadTempEliminationPass() {
    return std::make_unique<DeadTempEliminationPass>();
}

} // namespace compiler::ir
