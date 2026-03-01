#include "PassFactories.h"

#include <algorithm>
#include <optional>

namespace compiler::ir {

namespace {

std::optional<BinaryOp> InvertComparison(const BinaryOp op) {
    switch (op) {
    case BinaryOp::Equal:
        return BinaryOp::NotEqual;
    case BinaryOp::NotEqual:
        return BinaryOp::Equal;
    case BinaryOp::Less:
        return BinaryOp::GreaterEqual;
    case BinaryOp::LessEqual:
        return BinaryOp::Greater;
    case BinaryOp::Greater:
        return BinaryOp::LessEqual;
    case BinaryOp::GreaterEqual:
        return BinaryOp::Less;
    case BinaryOp::Add:
    case BinaryOp::Subtract:
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
    case BinaryOp::IntDivide:
    case BinaryOp::Modulo:
    case BinaryOp::Pow:
    case BinaryOp::BitwiseAnd:
    case BinaryOp::BitwiseOr:
    case BinaryOp::BitwiseXor:
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight:
        return std::nullopt;
    }
    return std::nullopt;
}

class BranchInversionPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "BranchInversionPass";
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
            auto *branch = std::get_if<BranchTerm>(&*block.terminator);
            if (branch == nullptr) {
                continue;
            }
            if (branch->true_target == branch->false_target) {
                continue;
            }
            const BlockId next_block = block.id + 1;
            if (branch->false_target != next_block ||
                next_block >= function.blocks.size()) {
                continue;
            }

            if (!block.instructions.empty() &&
                branch->condition.kind == ValueKind::Temp) {
                if (auto *cmp =
                        std::get_if<BinaryInst>(&block.instructions.back());
                    cmp != nullptr && cmp->dst == branch->condition.temp) {
                    if (const std::optional<BinaryOp> inverted =
                            InvertComparison(cmp->op);
                        inverted.has_value()) {
                        cmp->op = *inverted;
                        std::swap(branch->true_target, branch->false_target);
                        changed = true;
                        continue;
                    }
                }
            }

            const LocalId negated = function.next_temp++;
            block.instructions.push_back(UnaryInst{
                .dst = negated,
                .op = UnaryOp::LogicalNot,
                .value = branch->condition,
            });
            branch->condition = ValueRef::Temp(negated);
            std::swap(branch->true_target, branch->false_target);
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateBranchInversionPass() {
    return std::make_unique<BranchInversionPass>();
}

} // namespace compiler::ir
