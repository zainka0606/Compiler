#include "PassFactories.h"

#include <memory>
#include <string_view>

namespace compiler::bytecode {

namespace {

bool TryEvaluateTruthiness(const Operand &operand, bool &truthy_out) {
    switch (operand.kind) {
    case OperandKind::Register:
    case OperandKind::StackSlot:
        return false;
    case OperandKind::Number:
        truthy_out = operand.number != 0.0;
        return true;
    case OperandKind::String:
        truthy_out = !operand.text.empty();
        return true;
    case OperandKind::Bool:
        truthy_out = operand.boolean;
        return true;
    case OperandKind::Char:
        truthy_out = operand.character != '\0';
        return true;
    case OperandKind::Null:
        truthy_out = false;
        return true;
    }
    return false;
}

class ConstantBranchPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "ConstantBranchPass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        for (Instruction &inst : function.code) {
            if (inst.opcode != OpCode::JumpIfFalse) {
                continue;
            }
            bool truthy = false;
            if (!TryEvaluateTruthiness(inst.a, truthy)) {
                continue;
            }

            if (truthy) {
                inst.opcode = OpCode::Nop;
            } else {
                inst.opcode = OpCode::Jump;
            }
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateConstantBranchPass() {
    return std::make_unique<ConstantBranchPass>();
}

} // namespace compiler::bytecode
