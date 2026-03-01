#include "PassFactories.h"

#include "PassUtils.h"

#include <optional>
#include <unordered_set>

namespace compiler::ir {

namespace {

bool IsNumber(const ValueRef &value, const double number) {
    return value.kind == ValueKind::Number && value.number == number;
}

bool IsDefinitelyNumeric(const ValueRef &value,
                         const std::unordered_set<LocalId> &numeric_temps) {
    if (value.kind == ValueKind::Number || value.kind == ValueKind::Bool ||
        value.kind == ValueKind::Char) {
        return true;
    }
    return value.kind == ValueKind::Temp && numeric_temps.contains(value.temp);
}

std::optional<ValueRef>
TrySimplifyIdentity(const BinaryInst &inst,
                    const std::unordered_set<LocalId> &numeric_temps) {
    switch (inst.op) {
    case BinaryOp::Add:
        if (IsNumber(inst.lhs, 0.0) &&
            IsDefinitelyNumeric(inst.rhs, numeric_temps)) {
            return inst.rhs;
        }
        if (IsNumber(inst.rhs, 0.0) &&
            IsDefinitelyNumeric(inst.lhs, numeric_temps)) {
            return inst.lhs;
        }
        break;
    case BinaryOp::Subtract:
        if (IsNumber(inst.rhs, 0.0) &&
            IsDefinitelyNumeric(inst.lhs, numeric_temps)) {
            return inst.lhs;
        }
        break;
    case BinaryOp::Multiply:
        if (IsNumber(inst.lhs, 1.0) &&
            IsDefinitelyNumeric(inst.rhs, numeric_temps)) {
            return inst.rhs;
        }
        if (IsNumber(inst.rhs, 1.0) &&
            IsDefinitelyNumeric(inst.lhs, numeric_temps)) {
            return inst.lhs;
        }
        break;
    case BinaryOp::Divide:
        if (IsNumber(inst.rhs, 1.0) &&
            IsDefinitelyNumeric(inst.lhs, numeric_temps)) {
            return inst.lhs;
        }
        break;
    case BinaryOp::Pow:
        if (IsNumber(inst.rhs, 1.0) &&
            IsDefinitelyNumeric(inst.lhs, numeric_temps)) {
            return inst.lhs;
        }
        if (IsNumber(inst.rhs, 0.0) &&
            IsDefinitelyNumeric(inst.lhs, numeric_temps)) {
            return ValueRef::Number(1.0);
        }
        break;
    case BinaryOp::IntDivide:
    case BinaryOp::BitwiseAnd:
    case BinaryOp::BitwiseOr:
    case BinaryOp::BitwiseXor:
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight:
    case BinaryOp::Modulo:
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
        break;
    }
    return std::nullopt;
}

class IdentityArithmeticPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "IdentityArithmeticPass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            for (BasicBlock &block : function.blocks) {
                std::unordered_set<LocalId> numeric_temps;
                for (Instruction &instruction : block.instructions) {
                    auto *binary = std::get_if<BinaryInst>(&instruction);
                    if (binary != nullptr) {
                        const std::optional<ValueRef> simplified =
                            TrySimplifyIdentity(*binary, numeric_temps);
                        if (simplified.has_value()) {
                            const LocalId dst = binary->dst;
                            instruction =
                                MoveInst{.dst = dst, .src = *simplified};
                            changed = true;
                        }
                    }

                    if (const std::optional<LocalId> dst =
                            passes::detail::InstructionDestination(
                                instruction)) {
                        numeric_temps.erase(*dst);
                        std::visit(
                            [&](const auto &inst) {
                                using T = std::decay_t<decltype(inst)>;
                                if constexpr (std::is_same_v<T, MoveInst>) {
                                    if (IsDefinitelyNumeric(inst.src,
                                                            numeric_temps)) {
                                        numeric_temps.insert(inst.dst);
                                    }
                                } else if constexpr (std::is_same_v<
                                                         T, UnaryInst>) {
                                    numeric_temps.insert(inst.dst);
                                } else if constexpr (std::is_same_v<
                                                         T, BinaryInst>) {
                                    if (inst.op != BinaryOp::Add ||
                                        (IsDefinitelyNumeric(inst.lhs,
                                                             numeric_temps) &&
                                         IsDefinitelyNumeric(inst.rhs,
                                                             numeric_temps))) {
                                        numeric_temps.insert(inst.dst);
                                    }
                                }
                            },
                            instruction);
                    }
                }
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateIdentityArithmeticPass() {
    return std::make_unique<IdentityArithmeticPass>();
}

} // namespace compiler::ir
