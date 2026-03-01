#include "PassFactories.h"

#include "PassUtils.h"

namespace compiler::ir {

namespace {

class ConstantFoldingPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "ConstantFoldingPass";
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
            for (Instruction &instruction : block.instructions) {
                if (auto *unary = std::get_if<UnaryInst>(&instruction)) {
                    if (const std::optional<ValueRef> folded =
                            passes::detail::TryFoldUnary(unary->op,
                                                         unary->value)) {
                        instruction =
                            MoveInst{.dst = unary->dst, .src = *folded};
                        changed = true;
                        continue;
                    }
                }
                if (auto *binary = std::get_if<BinaryInst>(&instruction)) {
                    if (const std::optional<ValueRef> folded =
                            passes::detail::TryFoldBinary(
                                binary->op, binary->lhs, binary->rhs)) {
                        instruction =
                            MoveInst{.dst = binary->dst, .src = *folded};
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateConstantFoldingPass() {
    return std::make_unique<ConstantFoldingPass>();
}

} // namespace compiler::ir
