#include "PassFactories.h"

namespace compiler::ir {

namespace {

class LoadStoreForwardingPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "LoadStoreForwardingPass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            for (BasicBlock &block : function.blocks) {
                for (std::size_t i = 0; i + 1 < block.instructions.size();
                     ++i) {
                    const auto *store =
                        std::get_if<StoreLocalInst>(&block.instructions[i]);
                    auto *load =
                        std::get_if<LoadLocalInst>(&block.instructions[i + 1]);
                    if (store == nullptr || load == nullptr ||
                        store->slot != load->slot) {
                        continue;
                    }
                    block.instructions[i + 1] =
                        MoveInst{.dst = load->dst, .src = store->value};
                    changed = true;
                }
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateLoadStoreForwardingPass() {
    return std::make_unique<LoadStoreForwardingPass>();
}

} // namespace compiler::ir
