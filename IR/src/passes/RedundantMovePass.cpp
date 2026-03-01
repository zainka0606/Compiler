#include "PassFactories.h"

namespace compiler::ir {

namespace {

class RedundantMovePass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "RedundantMovePass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            for (BasicBlock &block : function.blocks) {
                std::vector<Instruction> kept;
                kept.reserve(block.instructions.size());
                for (Instruction &instruction : block.instructions) {
                    if (const auto *move =
                            std::get_if<MoveInst>(&instruction)) {
                        if (move->src.kind == ValueKind::Temp &&
                            move->src.temp == move->dst) {
                            changed = true;
                            continue;
                        }
                    }
                    kept.push_back(std::move(instruction));
                }
                block.instructions = std::move(kept);
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateRedundantMovePass() {
    return std::make_unique<RedundantMovePass>();
}

} // namespace compiler::ir
