#include "PassFactories.h"

#include <optional>
#include <string_view>

namespace compiler::bytecode {

namespace {

std::optional<OpCode> InvertConditionalJump(const OpCode opcode) {
    switch (opcode) {
    case OpCode::JumpCarry:
        return OpCode::JumpNotCarry;
    case OpCode::JumpNotCarry:
        return OpCode::JumpCarry;
    case OpCode::JumpZero:
        return OpCode::JumpNotZero;
    case OpCode::JumpNotZero:
        return OpCode::JumpZero;
    case OpCode::JumpSign:
        return OpCode::JumpNotSign;
    case OpCode::JumpNotSign:
        return OpCode::JumpSign;
    case OpCode::JumpOverflow:
        return OpCode::JumpNotOverflow;
    case OpCode::JumpNotOverflow:
        return OpCode::JumpOverflow;
    case OpCode::JumpAbove:
        return OpCode::JumpBelowEqual;
    case OpCode::JumpAboveEqual:
        return OpCode::JumpBelow;
    case OpCode::JumpBelow:
        return OpCode::JumpAboveEqual;
    case OpCode::JumpBelowEqual:
        return OpCode::JumpAbove;
    case OpCode::JumpGreater:
        return OpCode::JumpLessEqual;
    case OpCode::JumpGreaterEqual:
        return OpCode::JumpLess;
    case OpCode::JumpLess:
        return OpCode::JumpGreaterEqual;
    case OpCode::JumpLessEqual:
        return OpCode::JumpGreater;
    case OpCode::JumpIfFalse:
    case OpCode::Jump:
    case OpCode::Nop:
    case OpCode::Load:
    case OpCode::Store:
    case OpCode::Push:
    case OpCode::Pop:
    case OpCode::DeclareGlobal:
    case OpCode::Move:
    case OpCode::Unary:
    case OpCode::Binary:
    case OpCode::Compare:
    case OpCode::MakeArray:
    case OpCode::StackAllocObject:
    case OpCode::Call:
    case OpCode::CallRegister:
    case OpCode::Return:
        return std::nullopt;
    }
    return std::nullopt;
}

class CompareInversionPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "CompareInversionPass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        if (function.code.size() < 3) {
            return false;
        }

        for (Address pc = 0; pc + 2 < function.code.size(); ++pc) {
            Instruction &cmp = function.code[pc];
            Instruction &cond = function.code[pc + 1];
            Instruction &jump = function.code[pc + 2];
            if (cmp.opcode != OpCode::Compare) {
                continue;
            }
            if (jump.opcode != OpCode::Jump) {
                continue;
            }
            if (cond.opcode == OpCode::JumpIfFalse ||
                cond.opcode == OpCode::Jump) {
                continue;
            }
            const std::optional<OpCode> inverted =
                InvertConditionalJump(cond.opcode);
            if (!inverted.has_value()) {
                continue;
            }

            const Address fallthrough = pc + 2;
            if (cond.target != fallthrough || jump.target == fallthrough) {
                continue;
            }

            cond.opcode = *inverted;
            cond.target = jump.target;
            jump.opcode = OpCode::Nop;
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateCompareInversionPass() {
    return std::make_unique<CompareInversionPass>();
}

} // namespace compiler::bytecode

