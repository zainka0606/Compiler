#include "PassFactories.h"

#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace compiler::ir {

namespace {

struct InlineExpansion {
    std::vector<Instruction> instructions;
    ValueRef return_value = ValueRef::Null();
};

class InliningPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "InliningPass";
    }

    bool Run(Program &program) override {
        std::unordered_map<std::string, const Function *> candidates;
        for (const Function &function : program.functions) {
            if (!IsInlinable(function)) {
                continue;
            }
            candidates.emplace(function.name, &function);
        }

        bool changed = false;
        for (Function &function : program.functions) {
            changed |= RunOnFunction(function, candidates);
        }
        return changed;
    }

  private:
    static constexpr std::size_t kMaxInlineInstructionCount = 24;

    static bool IsInlinable(const Function &function) {
        if (function.name == "__main__" || function.is_method) {
            return false;
        }
        for (const std::string_view suffix : {".__new", ".__new_heap"}) {
            if (function.name.size() > suffix.size() &&
                function.name.substr(function.name.size() - suffix.size()) ==
                    suffix) {
                return false;
            }
        }
        if (function.blocks.size() != 1) {
            return false;
        }
        const BasicBlock &block = function.blocks.front();
        if (!block.terminator.has_value()) {
            return false;
        }
        if (!std::holds_alternative<ReturnTerm>(*block.terminator)) {
            return false;
        }
        if (block.instructions.size() > kMaxInlineInstructionCount) {
            return false;
        }
        for (const Instruction &instruction : block.instructions) {
            if (const auto *call = std::get_if<CallInst>(&instruction)) {
                if (call->callee == function.name) {
                    return false;
                }
            }
        }
        return true;
    }

    static std::optional<ValueRef>
    RemapValue(const ValueRef &value,
               const std::unordered_map<LocalId, LocalId> &temp_map) {
        if (value.kind != ValueKind::Temp) {
            return value;
        }
        const auto it = temp_map.find(value.temp);
        if (it == temp_map.end()) {
            return std::nullopt;
        }
        return ValueRef::Temp(it->second);
    }

    static SlotId AllocateInlineSlot(Function &caller, const std::string &name,
                                     const std::string &type_name,
                                     const std::string &callee_name,
                                     const SlotId callee_slot) {
        const SlotId slot = caller.next_slot++;
        caller.slot_names.push_back("inline$" + callee_name + "$" + name +
                                    "$" + std::to_string(callee_slot));
        caller.slot_types.push_back(type_name);
        return slot;
    }

    static std::optional<InlineExpansion>
    ExpandInline(const CallInst &call, const Function &callee, Function &caller) {
        if (call.args.size() != callee.params.size()) {
            return std::nullopt;
        }

        std::unordered_map<SlotId, SlotId> slot_map;
        slot_map.reserve(callee.next_slot);
        for (SlotId callee_slot = 0; callee_slot < callee.next_slot;
             ++callee_slot) {
            const std::string slot_name =
                (callee_slot < callee.slot_names.size())
                    ? callee.slot_names[callee_slot]
                    : ("s" + std::to_string(callee_slot));
            const std::string slot_type =
                (callee_slot < callee.slot_types.size())
                    ? callee.slot_types[callee_slot]
                    : std::string{};
            slot_map.emplace(callee_slot,
                             AllocateInlineSlot(caller, slot_name, slot_type,
                                                callee.name, callee_slot));
        }

        InlineExpansion expansion;
        for (std::size_t i = 0; i < callee.params.size(); ++i) {
            const SlotId callee_param_slot = callee.param_slots[i];
            const auto mapped_slot = slot_map.find(callee_param_slot);
            if (mapped_slot == slot_map.end()) {
                return std::nullopt;
            }
            expansion.instructions.push_back(
                StoreLocalInst{.slot = mapped_slot->second, .value = call.args[i]});
        }

        std::unordered_map<LocalId, LocalId> temp_map;
        temp_map.reserve(callee.next_temp);

        const BasicBlock &callee_block = callee.blocks.front();
        for (const Instruction &instruction : callee_block.instructions) {
            bool ok = true;
            std::optional<Instruction> rewritten;

            rewritten = std::visit(
                [&](const auto &inst) -> std::optional<Instruction> {
                    using T = std::decay_t<decltype(inst)>;
                    if constexpr (std::is_same_v<T, LoadLocalInst>) {
                        const auto mapped_slot = slot_map.find(inst.slot);
                        if (mapped_slot == slot_map.end()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return LoadLocalInst{.dst = new_dst,
                                             .slot = mapped_slot->second};
                    } else if constexpr (std::is_same_v<T, StoreLocalInst>) {
                        const auto mapped_slot = slot_map.find(inst.slot);
                        const std::optional<ValueRef> value =
                            RemapValue(inst.value, temp_map);
                        if (mapped_slot == slot_map.end() || !value.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        return StoreLocalInst{.slot = mapped_slot->second,
                                              .value = *value};
                    } else if constexpr (std::is_same_v<T, DeclareGlobalInst>) {
                        const std::optional<ValueRef> value =
                            RemapValue(inst.value, temp_map);
                        if (!value.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        return DeclareGlobalInst{.name = inst.name,
                                                 .type_name = inst.type_name,
                                                 .value = *value};
                    } else if constexpr (std::is_same_v<T, LoadGlobalInst>) {
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return LoadGlobalInst{.dst = new_dst,
                                              .name = inst.name};
                    } else if constexpr (std::is_same_v<T, StoreGlobalInst>) {
                        const std::optional<ValueRef> value =
                            RemapValue(inst.value, temp_map);
                        if (!value.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        return StoreGlobalInst{.name = inst.name, .value = *value};
                    } else if constexpr (std::is_same_v<T, MoveInst>) {
                        const std::optional<ValueRef> src =
                            RemapValue(inst.src, temp_map);
                        if (!src.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return MoveInst{.dst = new_dst, .src = *src};
                    } else if constexpr (std::is_same_v<T, UnaryInst>) {
                        const std::optional<ValueRef> value =
                            RemapValue(inst.value, temp_map);
                        if (!value.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return UnaryInst{.dst = new_dst,
                                         .op = inst.op,
                                         .value = *value};
                    } else if constexpr (std::is_same_v<T, BinaryInst>) {
                        const std::optional<ValueRef> lhs =
                            RemapValue(inst.lhs, temp_map);
                        const std::optional<ValueRef> rhs =
                            RemapValue(inst.rhs, temp_map);
                        if (!lhs.has_value() || !rhs.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return BinaryInst{.dst = new_dst,
                                          .op = inst.op,
                                          .lhs = *lhs,
                                          .rhs = *rhs};
                    } else if constexpr (std::is_same_v<T, MakeArrayInst>) {
                        std::vector<ValueRef> elements;
                        elements.reserve(inst.elements.size());
                        for (const ValueRef &element : inst.elements) {
                            const std::optional<ValueRef> value =
                                RemapValue(element, temp_map);
                            if (!value.has_value()) {
                                ok = false;
                                return std::nullopt;
                            }
                            elements.push_back(*value);
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return MakeArrayInst{.dst = new_dst,
                                             .elements = std::move(elements)};
                    } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                        const std::optional<ValueRef> array =
                            RemapValue(inst.array, temp_map);
                        const std::optional<ValueRef> index =
                            RemapValue(inst.index, temp_map);
                        if (!array.has_value() || !index.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return ArrayLoadInst{.dst = new_dst,
                                             .array = *array,
                                             .index = *index};
                    } else if constexpr (std::is_same_v<T, ArrayStoreInst>) {
                        const std::optional<ValueRef> array =
                            RemapValue(inst.array, temp_map);
                        const std::optional<ValueRef> index =
                            RemapValue(inst.index, temp_map);
                        const std::optional<ValueRef> value =
                            RemapValue(inst.value, temp_map);
                        if (!array.has_value() || !index.has_value() ||
                            !value.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        return ArrayStoreInst{.array = *array,
                                              .index = *index,
                                              .value = *value};
                    } else if constexpr (std::is_same_v<T,
                                                        ResolveFieldOffsetInst>) {
                        const std::optional<ValueRef> object =
                            RemapValue(inst.object, temp_map);
                        if (!object.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return ResolveFieldOffsetInst{.dst = new_dst,
                                                      .object = *object,
                                                      .member = inst.member};
                    } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                        const std::optional<ValueRef> object =
                            RemapValue(inst.object, temp_map);
                        const std::optional<ValueRef> offset =
                            RemapValue(inst.offset, temp_map);
                        if (!object.has_value() || !offset.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return ObjectLoadInst{.dst = new_dst,
                                              .object = *object,
                                              .offset = *offset};
                    } else if constexpr (std::is_same_v<T, ObjectStoreInst>) {
                        const std::optional<ValueRef> object =
                            RemapValue(inst.object, temp_map);
                        const std::optional<ValueRef> offset =
                            RemapValue(inst.offset, temp_map);
                        const std::optional<ValueRef> value =
                            RemapValue(inst.value, temp_map);
                        if (!object.has_value() || !offset.has_value() ||
                            !value.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        return ObjectStoreInst{.object = *object,
                                               .offset = *offset,
                                               .value = *value};
                    } else if constexpr (std::is_same_v<T,
                                                        ResolveMethodSlotInst>) {
                        const std::optional<ValueRef> object =
                            RemapValue(inst.object, temp_map);
                        if (!object.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return ResolveMethodSlotInst{.dst = new_dst,
                                                     .object = *object,
                                                     .method = inst.method};
                    } else if constexpr (std::is_same_v<T, CallInst>) {
                        std::vector<ValueRef> args;
                        args.reserve(inst.args.size());
                        for (const ValueRef &arg : inst.args) {
                            const std::optional<ValueRef> value =
                                RemapValue(arg, temp_map);
                            if (!value.has_value()) {
                                ok = false;
                                return std::nullopt;
                            }
                            args.push_back(*value);
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return CallInst{.dst = new_dst,
                                        .callee = inst.callee,
                                        .args = std::move(args)};
                    } else if constexpr (std::is_same_v<T, VirtualCallInst>) {
                        const std::optional<ValueRef> object =
                            RemapValue(inst.object, temp_map);
                        const std::optional<ValueRef> slot =
                            RemapValue(inst.slot, temp_map);
                        if (!object.has_value() || !slot.has_value()) {
                            ok = false;
                            return std::nullopt;
                        }
                        std::vector<ValueRef> args;
                        args.reserve(inst.args.size());
                        for (const ValueRef &arg : inst.args) {
                            const std::optional<ValueRef> value =
                                RemapValue(arg, temp_map);
                            if (!value.has_value()) {
                                ok = false;
                                return std::nullopt;
                            }
                            args.push_back(*value);
                        }
                        const LocalId new_dst = caller.next_temp++;
                        temp_map[inst.dst] = new_dst;
                        return VirtualCallInst{.dst = new_dst,
                                               .object = *object,
                                               .slot = *slot,
                                               .args = std::move(args)};
                    }
                    return std::nullopt;
                },
                instruction);

            if (!ok || !rewritten.has_value()) {
                return std::nullopt;
            }
            expansion.instructions.push_back(std::move(*rewritten));
        }

        const ReturnTerm *ret = std::get_if<ReturnTerm>(
            &*callee_block.terminator);
        if (ret == nullptr) {
            return std::nullopt;
        }

        if (!ret->value.has_value()) {
            expansion.return_value = ValueRef::Null();
            return expansion;
        }

        const std::optional<ValueRef> remapped =
            RemapValue(*ret->value, temp_map);
        if (!remapped.has_value()) {
            return std::nullopt;
        }

        expansion.return_value = *remapped;
        return expansion;
    }

    static bool RunOnFunction(
        Function &function,
        const std::unordered_map<std::string, const Function *> &candidates) {
        bool changed = false;

        for (BasicBlock &block : function.blocks) {
            std::vector<Instruction> rewritten;
            rewritten.reserve(block.instructions.size());

            for (Instruction &instruction : block.instructions) {
                const auto *call = std::get_if<CallInst>(&instruction);
                if (call == nullptr || call->callee == function.name) {
                    rewritten.push_back(std::move(instruction));
                    continue;
                }

                const auto candidate_it = candidates.find(call->callee);
                if (candidate_it == candidates.end()) {
                    rewritten.push_back(std::move(instruction));
                    continue;
                }

                const std::optional<InlineExpansion> expansion =
                    ExpandInline(*call, *candidate_it->second, function);
                if (!expansion.has_value()) {
                    rewritten.push_back(std::move(instruction));
                    continue;
                }

                changed = true;
                for (const Instruction &inlined : expansion->instructions) {
                    rewritten.push_back(inlined);
                }
                rewritten.push_back(MoveInst{.dst = call->dst,
                                             .src = expansion->return_value});
            }

            block.instructions = std::move(rewritten);
        }

        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateInliningPass() {
    return std::make_unique<InliningPass>();
}

} // namespace compiler::ir
