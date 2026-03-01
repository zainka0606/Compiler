#include "PassFactories.h"
#include "PassUtils.h"

#include <memory>
#include <string_view>
#include <vector>

namespace compiler::bytecode {

namespace {

bool CanForwardIntoLoad(const Instruction &store, const Instruction &load) {
    if (store.opcode != OpCode::Store || load.opcode != OpCode::Load) {
        return false;
    }

    if (store.store_mode == StoreMode::StackRelative &&
        load.load_mode == LoadMode::StackRelative) {
        return store.slot == load.slot;
    }
    if (store.store_mode == StoreMode::StackAbsolute &&
        load.load_mode == LoadMode::StackAbsolute) {
        return store.slot == load.slot;
    }
    if (store.store_mode == StoreMode::Global &&
        load.load_mode == LoadMode::Global) {
        return store.text == load.text;
    }
    return false;
}

class LoadStoreForwardingPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "LoadStoreForwardingPass";
    }

    bool Run(Function &function) override {
        bool changed = false;
        std::vector<bool> is_jump_target(function.code.size(), false);
        for (const Instruction &inst : function.code) {
            if (IsJumpOpcode(inst.opcode) && inst.target < function.code.size()) {
                is_jump_target[inst.target] = true;
            }
        }

        for (std::size_t i = 0; i + 1 < function.code.size(); ++i) {
            Instruction &store = function.code[i];
            Instruction &load = function.code[i + 1];
            if (is_jump_target[i + 1]) {
                continue;
            }
            if (!CanForwardIntoLoad(store, load)) {
                continue;
            }
            load.opcode = OpCode::Move;
            load.a = store.a;
            changed = true;
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateLoadStoreForwardingPass() {
    return std::make_unique<LoadStoreForwardingPass>();
}

} // namespace compiler::bytecode
