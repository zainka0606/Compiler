#include "PassFactories.h"

#include "PassUtils.h"

#include <unordered_map>

namespace compiler::ir {

namespace {

class CopyPropagationPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "CopyPropagationPass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            changed |= RunOnFunction(function);
        }
        return changed;
    }

  private:
    static bool
    RewriteValue(ValueRef &value,
                 const std::unordered_map<LocalId, ValueRef> &copies) {
        const ValueRef rewritten =
            passes::detail::ResolveValueRef(value, copies);
        if (rewritten.kind == value.kind && rewritten.temp == value.temp &&
            rewritten.number == value.number && rewritten.text == value.text &&
            rewritten.boolean == value.boolean &&
            rewritten.character == value.character) {
            return false;
        }
        value = rewritten;
        return true;
    }

    static bool RunOnFunction(Function &function) {
        bool changed = false;
        for (BasicBlock &block : function.blocks) {
            std::unordered_map<LocalId, ValueRef> copies;

            for (Instruction &instruction : block.instructions) {
                changed |= std::visit(
                    [&](auto &inst) -> bool {
                        using T = std::decay_t<decltype(inst)>;
                        bool local_change = false;
                        if constexpr (std::is_same_v<T, StoreLocalInst>) {
                            local_change |= RewriteValue(inst.value, copies);
                        } else if constexpr (std::is_same_v<
                                                 T, DeclareGlobalInst>) {
                            local_change |= RewriteValue(inst.value, copies);
                        } else if constexpr (std::is_same_v<T,
                                                            StoreGlobalInst>) {
                            local_change |= RewriteValue(inst.value, copies);
                        } else if constexpr (std::is_same_v<T, MoveInst>) {
                            local_change |= RewriteValue(inst.src, copies);
                        } else if constexpr (std::is_same_v<T, UnaryInst>) {
                            local_change |= RewriteValue(inst.value, copies);
                        } else if constexpr (std::is_same_v<T, BinaryInst>) {
                            local_change |= RewriteValue(inst.lhs, copies);
                            local_change |= RewriteValue(inst.rhs, copies);
                        } else if constexpr (std::is_same_v<T, MakeArrayInst>) {
                            for (ValueRef &value : inst.elements) {
                                local_change |= RewriteValue(value, copies);
                            }
                        } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                            local_change |= RewriteValue(inst.array, copies);
                            local_change |= RewriteValue(inst.index, copies);
                        } else if constexpr (std::is_same_v<T,
                                                            ArrayStoreInst>) {
                            local_change |= RewriteValue(inst.array, copies);
                            local_change |= RewriteValue(inst.index, copies);
                            local_change |= RewriteValue(inst.value, copies);
                        } else if constexpr (std::is_same_v<
                                                 T, ResolveFieldOffsetInst>) {
                            local_change |= RewriteValue(inst.object, copies);
                        } else if constexpr (std::is_same_v<T,
                                                            ObjectLoadInst>) {
                            local_change |= RewriteValue(inst.object, copies);
                            local_change |= RewriteValue(inst.offset, copies);
                        } else if constexpr (std::is_same_v<T,
                                                            ObjectStoreInst>) {
                            local_change |= RewriteValue(inst.object, copies);
                            local_change |= RewriteValue(inst.offset, copies);
                            local_change |= RewriteValue(inst.value, copies);
                        } else if constexpr (std::is_same_v<
                                                 T, ResolveMethodSlotInst>) {
                            local_change |= RewriteValue(inst.object, copies);
                        } else if constexpr (std::is_same_v<T, CallInst>) {
                            for (ValueRef &value : inst.args) {
                                local_change |= RewriteValue(value, copies);
                            }
                        } else if constexpr (std::is_same_v<T,
                                                            VirtualCallInst>) {
                            local_change |= RewriteValue(inst.object, copies);
                            local_change |= RewriteValue(inst.slot, copies);
                            for (ValueRef &value : inst.args) {
                                local_change |= RewriteValue(value, copies);
                            }
                        }
                        return local_change;
                    },
                    instruction);

                if (const std::optional<LocalId> dst =
                        passes::detail::InstructionDestination(instruction)) {
                    copies.erase(*dst);
                    if (const auto *move =
                            std::get_if<MoveInst>(&instruction)) {
                        copies[*dst] = move->src;
                    }
                }
            }

            if (!block.terminator.has_value()) {
                continue;
            }
            changed |= std::visit(
                [&](auto &term) -> bool {
                    using T = std::decay_t<decltype(term)>;
                    bool local_change = false;
                    if constexpr (std::is_same_v<T, BranchTerm>) {
                        local_change |= RewriteValue(term.condition, copies);
                    } else if constexpr (std::is_same_v<T, ReturnTerm>) {
                        if (term.value.has_value()) {
                            local_change |= RewriteValue(*term.value, copies);
                        }
                    }
                    return local_change;
                },
                *block.terminator);
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateCopyPropagationPass() {
    return std::make_unique<CopyPropagationPass>();
}

} // namespace compiler::ir
