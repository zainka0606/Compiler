#include "PassFactories.h"

#include <memory>
#include <string_view>

namespace compiler::bytecode {

namespace {

class PushPopFoldPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "PushPopFoldPass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        for (std::size_t i = 0; i + 1 < function.code.size(); ++i) {
            Instruction &first = function.code[i];
            Instruction &second = function.code[i + 1];
            if (first.opcode != OpCode::Push || second.opcode != OpCode::Pop) {
                continue;
            }

            if (second.dst == kInvalidRegister &&
                second.dst_slot == kInvalidSlot) {
                first.opcode = OpCode::Nop;
                second.opcode = OpCode::Nop;
                changed = true;
                continue;
            }

            second.opcode = OpCode::Move;
            second.a = first.a;
            first.opcode = OpCode::Nop;
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreatePushPopFoldPass() {
    return std::make_unique<PushPopFoldPass>();
}

} // namespace compiler::bytecode
