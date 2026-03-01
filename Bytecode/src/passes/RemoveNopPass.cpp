#include "PassFactories.h"
#include "PassUtils.h"

#include <memory>
#include <string_view>
#include <vector>

namespace compiler::bytecode {

namespace {

class RemoveNopPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "RemoveNopPass";
    }

    bool Run(Function &function) override {
        if (function.code.empty()) {
            return false;
        }

        std::vector<Address> remap(function.code.size(), 0);
        std::vector<Instruction> compacted;
        compacted.reserve(function.code.size());

        for (Address old_pc = 0; old_pc < function.code.size(); ++old_pc) {
            remap[old_pc] = static_cast<Address>(compacted.size());
            if (function.code[old_pc].opcode == OpCode::Nop) {
                continue;
            }
            compacted.push_back(function.code[old_pc]);
        }

        if (compacted.size() == function.code.size()) {
            return false;
        }

        for (Instruction &inst : compacted) {
            if (IsJumpOpcode(inst.opcode)) {
                if (inst.target >= remap.size()) {
                    throw BytecodeException(
                        "invalid jump target while compacting nops");
                }
                inst.target = remap[inst.target];
            }
        }

        if (function.entry_pc >= remap.size()) {
            throw BytecodeException("invalid entry PC while compacting nops");
        }
        function.entry_pc = remap[function.entry_pc];
        function.code = std::move(compacted);
        return true;
    }
};

} // namespace

std::unique_ptr<Pass> CreateRemoveNopPass() {
    return std::make_unique<RemoveNopPass>();
}

} // namespace compiler::bytecode
