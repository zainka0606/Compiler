#include "PassFactories.h"

#include "PassUtils.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace compiler::ir {

namespace {

std::optional<std::string>
ClassNameFromAllocatorFunction(const std::string_view callee_name) {
    for (const std::string_view suffix : {".__new", ".__new_heap"}) {
        if (callee_name.size() <= suffix.size()) {
            continue;
        }
        if (callee_name.substr(callee_name.size() - suffix.size()) == suffix) {
            return std::string(
                callee_name.substr(0, callee_name.size() - suffix.size()));
        }
    }
    return std::nullopt;
}

std::optional<SlotId>
TryResolveSlotValue(const ValueRef &value,
                    const std::unordered_map<LocalId, SlotId> &temp_slots) {
    if (value.kind == ValueKind::Temp) {
        const auto found = temp_slots.find(value.temp);
        if (found != temp_slots.end()) {
            return found->second;
        }
        return std::nullopt;
    }
    if (value.kind != ValueKind::Number) {
        return std::nullopt;
    }
    if (!std::isfinite(value.number)) {
        return std::nullopt;
    }
    const double floored = std::floor(value.number);
    if (floored != value.number || floored < 0.0 ||
        floored > static_cast<double>(std::numeric_limits<SlotId>::max())) {
        return std::nullopt;
    }
    return static_cast<SlotId>(floored);
}

class DevirtualizeCallPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "DevirtualizeCallPass";
    }

    bool Run(Program &program) override {
        class_by_name_.clear();
        class_by_name_.reserve(program.classes.size());
        classes_with_derived_.clear();
        for (const ClassInfo &class_info : program.classes) {
            class_by_name_[class_info.name] = &class_info;
            if (!class_info.base_class.empty()) {
                classes_with_derived_.insert(class_info.base_class);
            }
        }

        bool changed = false;
        for (Function &function : program.functions) {
            changed |= RunOnFunction(function);
        }
        return changed;
    }

  private:
    std::unordered_map<std::string, const ClassInfo *> class_by_name_;
    std::unordered_set<std::string> classes_with_derived_;

    [[nodiscard]] bool IsSubtype(const std::string_view derived,
                                 const std::string_view base) const {
        if (derived.empty() || base.empty()) {
            return false;
        }
        if (derived == base) {
            return true;
        }

        std::unordered_set<std::string> visited;
        std::string current(derived);
        while (!current.empty()) {
            if (current == base) {
                return true;
            }
            if (!visited.insert(current).second) {
                return false;
            }
            const auto class_it = class_by_name_.find(current);
            if (class_it == class_by_name_.end() || class_it->second == nullptr) {
                return false;
            }
            current = class_it->second->base_class;
        }
        return false;
    }

    void MergeInferredType(std::string &existing,
                           const std::string &incoming) const {
        if (incoming.empty()) {
            return;
        }
        if (existing.empty()) {
            existing = incoming;
            return;
        }
        if (existing == incoming) {
            return;
        }
        if (IsSubtype(incoming, existing)) {
            return;
        }
        if (IsSubtype(existing, incoming)) {
            existing = incoming;
            return;
        }
        existing.clear();
    }

    std::optional<std::string>
    TypeOfValue(const ValueRef &value,
                const std::unordered_map<LocalId, std::string> &temp_types) const {
        if (value.kind != ValueKind::Temp) {
            return std::nullopt;
        }
        const auto found = temp_types.find(value.temp);
        if (found == temp_types.end() || found->second.empty()) {
            return std::nullopt;
        }
        return found->second;
    }

    bool RunOnFunction(Function &function) const {
        std::unordered_map<SlotId, std::string> inferred_slot_types;
        inferred_slot_types.reserve(function.slot_types.size());
        for (SlotId slot = 0; slot < function.slot_types.size(); ++slot) {
            if (!function.slot_types[slot].empty()) {
                inferred_slot_types.emplace(slot, function.slot_types[slot]);
            }
        }
        if (function.is_method && !function.owning_class.empty()) {
            for (SlotId slot = 0; slot < function.slot_names.size(); ++slot) {
                if (function.slot_names[slot] == "self") {
                    inferred_slot_types[slot] = function.owning_class;
                }
            }
        }

        bool changed = false;
        std::unordered_map<std::string, std::string> inferred_global_types;
        for (BasicBlock &block : function.blocks) {
            std::unordered_map<LocalId, std::string> temp_types;
            std::unordered_map<LocalId, SlotId> temp_slots;
            temp_types.reserve(function.next_temp);
            temp_slots.reserve(function.next_temp);

            for (Instruction &instruction : block.instructions) {
                if (auto *virtual_call = std::get_if<VirtualCallInst>(&instruction)) {
                    const std::optional<std::string> object_type =
                        TypeOfValue(virtual_call->object, temp_types);
                    const std::optional<SlotId> slot =
                        TryResolveSlotValue(virtual_call->slot, temp_slots);
                    if (object_type.has_value() && slot.has_value()) {
                        if (classes_with_derived_.contains(*object_type)) {
                            continue;
                        }
                        const auto class_it = class_by_name_.find(*object_type);
                        if (class_it != class_by_name_.end() &&
                            *slot < class_it->second->vtable_functions.size()) {
                            CallInst direct;
                            direct.dst = virtual_call->dst;
                            direct.callee =
                                class_it->second->vtable_functions[*slot];
                            direct.args.reserve(virtual_call->args.size() + 1);
                            direct.args.push_back(virtual_call->object);
                            for (const ValueRef &arg : virtual_call->args) {
                                direct.args.push_back(arg);
                            }
                            instruction = std::move(direct);
                            changed = true;
                        }
                    }
                }

                if (const std::optional<LocalId> dst =
                        passes::detail::InstructionDestination(instruction);
                    dst.has_value()) {
                    temp_types.erase(*dst);
                    temp_slots.erase(*dst);
                }

                std::visit(
                    [&](const auto &inst) {
                        using T = std::decay_t<decltype(inst)>;
                        if constexpr (std::is_same_v<T, LoadLocalInst>) {
                            const auto slot_it =
                                inferred_slot_types.find(inst.slot);
                            if (slot_it != inferred_slot_types.end() &&
                                !slot_it->second.empty()) {
                                temp_types[inst.dst] = slot_it->second;
                            }
                        } else if constexpr (std::is_same_v<T, StoreLocalInst>) {
                            const std::optional<std::string> value_type =
                                TypeOfValue(inst.value, temp_types);
                            if (!value_type.has_value() || value_type->empty()) {
                                return;
                            }
                            auto slot_it = inferred_slot_types.find(inst.slot);
                            if (slot_it == inferred_slot_types.end()) {
                                inferred_slot_types.emplace(inst.slot, *value_type);
                                return;
                            }
                            MergeInferredType(slot_it->second, *value_type);
                        } else if constexpr (std::is_same_v<T, MoveInst>) {
                            if (inst.src.kind == ValueKind::Temp) {
                                if (const auto source_type =
                                        temp_types.find(inst.src.temp);
                                    source_type != temp_types.end()) {
                                    temp_types[inst.dst] = source_type->second;
                                }
                                if (const auto source_slot =
                                        temp_slots.find(inst.src.temp);
                                    source_slot != temp_slots.end()) {
                                    temp_slots[inst.dst] = source_slot->second;
                                }
                                return;
                            }
                            if (const std::optional<SlotId> slot =
                                    TryResolveSlotValue(inst.src, temp_slots);
                                slot.has_value()) {
                                temp_slots[inst.dst] = *slot;
                            }
                        } else if constexpr (std::is_same_v<T, DeclareGlobalInst>) {
                            const std::optional<std::string> value_type =
                                TypeOfValue(inst.value, temp_types);
                            if (value_type.has_value() && !value_type->empty()) {
                                inferred_global_types[inst.name] = *value_type;
                            }
                        } else if constexpr (std::is_same_v<T, LoadGlobalInst>) {
                            const auto global_it =
                                inferred_global_types.find(inst.name);
                            if (global_it != inferred_global_types.end()) {
                                temp_types[inst.dst] = global_it->second;
                            }
                        } else if constexpr (std::is_same_v<T, StoreGlobalInst>) {
                            const std::optional<std::string> value_type =
                                TypeOfValue(inst.value, temp_types);
                            if (value_type.has_value() && !value_type->empty()) {
                                MergeInferredType(inferred_global_types[inst.name],
                                                  *value_type);
                            }
                        } else if constexpr (std::is_same_v<T, CallInst>) {
                            if ((inst.callee == "__new" ||
                                 inst.callee == "alloc" ||
                                 inst.callee == "stack_alloc") &&
                                !inst.args.empty() &&
                                inst.args[0].kind == ValueKind::String &&
                                class_by_name_.contains(inst.args[0].text)) {
                                temp_types[inst.dst] = inst.args[0].text;
                                return;
                            }
                            if (const std::optional<std::string> class_name =
                                    ClassNameFromAllocatorFunction(inst.callee);
                                class_name.has_value() &&
                                class_by_name_.contains(*class_name)) {
                                temp_types[inst.dst] = *class_name;
                                return;
                            }
                            if (class_by_name_.contains(inst.callee)) {
                                temp_types[inst.dst] = inst.callee;
                            }
                        } else if constexpr (std::is_same_v<T, VirtualCallInst>) {
                            if (const std::optional<std::string> object_type =
                                    TypeOfValue(inst.object, temp_types)) {
                                temp_types[inst.dst] = *object_type;
                            }
                        } else if constexpr (std::is_same_v<T, ResolveMethodSlotInst>) {
                            if (const std::optional<std::string> object_type =
                                    TypeOfValue(inst.object, temp_types)) {
                                const auto class_it =
                                    class_by_name_.find(*object_type);
                                if (class_it != class_by_name_.end()) {
                                    const auto slot_it =
                                        class_it->second->method_slots.find(
                                            inst.method);
                                    if (slot_it !=
                                        class_it->second->method_slots.end()) {
                                        temp_slots[inst.dst] = slot_it->second;
                                    }
                                }
                            }
                        }
                    },
                    instruction);
            }
        }

        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateDevirtualizeCallPass() {
    return std::make_unique<DevirtualizeCallPass>();
}

} // namespace compiler::ir
