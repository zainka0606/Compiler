#include "PassFactories.h"

#include <memory>
#include <string_view>

namespace compiler::bytecode {

namespace {

class RedundantMovePass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "RedundantMovePass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        for (Instruction &inst : function.code) {
            if (inst.opcode != OpCode::Move) {
                continue;
            }
            if (inst.dst != kInvalidRegister &&
                inst.a.kind == OperandKind::Register && inst.a.reg == inst.dst) {
                inst.opcode = OpCode::Nop;
                changed = true;
                continue;
            }
            if (inst.dst == kInvalidRegister && inst.dst_slot != kInvalidSlot &&
                inst.a.kind == OperandKind::StackSlot &&
                inst.a.stack_slot == inst.dst_slot) {
                inst.opcode = OpCode::Nop;
                changed = true;
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateRedundantMovePass() {
    return std::make_unique<RedundantMovePass>();
}

} // namespace compiler::bytecode
