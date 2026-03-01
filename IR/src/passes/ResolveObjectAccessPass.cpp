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

class ResolveObjectAccessPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "ResolveObjectAccessPass";
    }

    bool Run(Program &program) override {
        class_by_name_.clear();
        class_by_name_.reserve(program.classes.size());
        for (const ClassInfo &class_info : program.classes) {
            class_by_name_[class_info.name] = &class_info;
        }

        bool changed = false;
        for (Function &function : program.functions) {
            changed |= RunOnFunction(function);
        }
        return changed;
    }

  private:
    std::unordered_map<std::string, const ClassInfo *> class_by_name_;

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
        switch (value.kind) {
        case ValueKind::Temp: {
            const auto temp_it = temp_types.find(value.temp);
            if (temp_it == temp_types.end() || temp_it->second.empty()) {
                return std::nullopt;
            }
            return temp_it->second;
        }
        case ValueKind::Number:
            return std::string("number");
        case ValueKind::String:
            return std::string("string");
        case ValueKind::Bool:
            return std::string("bool");
        case ValueKind::Char:
            return std::string("char");
        case ValueKind::Null:
            return std::string("null");
        }
        return std::nullopt;
    }

    static bool TryIntegralNumber(const ValueRef &value, SlotId &out_slot) {
        if (value.kind != ValueKind::Number) {
            return false;
        }
        const double as_double = value.number;
        if (!std::isfinite(as_double)) {
            return false;
        }
        const double floored = std::floor(as_double);
        if (floored != as_double || floored < 0.0 ||
            floored > static_cast<double>(std::numeric_limits<SlotId>::max())) {
            return false;
        }
        out_slot = static_cast<SlotId>(floored);
        return true;
    }

    bool RunOnFunction(Function &function) const {
        std::unordered_map<SlotId, std::string> inferred_slot_types;
        inferred_slot_types.reserve(function.slot_types.size());
        for (SlotId slot = 0; slot < function.slot_types.size(); ++slot) {
            if (!function.slot_types[slot].empty()) {
                inferred_slot_types[slot] = function.slot_types[slot];
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
            temp_types.reserve(function.next_temp);

            for (Instruction &instruction : block.instructions) {
                if (auto *field =
                        std::get_if<ResolveFieldOffsetInst>(&instruction)) {
                    std::optional<std::string> resolved_class_name =
                        TypeOfValue(field->object, temp_types);

                    if (!resolved_class_name.has_value()) {
                        std::optional<std::string> unique_owner;
                        for (const auto &[candidate_name, candidate_class] :
                             class_by_name_) {
                            if (candidate_class->field_offsets.contains(
                                    field->member)) {
                                if (unique_owner.has_value()) {
                                    unique_owner.reset();
                                    break;
                                }
                                unique_owner = candidate_name;
                            }
                        }
                        resolved_class_name = unique_owner;
                    }

                    if (resolved_class_name.has_value()) {
                        const auto class_it =
                            class_by_name_.find(*resolved_class_name);
                        if (class_it != class_by_name_.end()) {
                            const auto field_it =
                                class_it->second->field_offsets.find(
                                    field->member);
                            if (field_it !=
                                class_it->second->field_offsets.end()) {
                                const LocalId object_temp =
                                    field->object.kind == ValueKind::Temp
                                        ? field->object.temp
                                        : kInvalidLocal;
                                instruction = MoveInst{
                                    .dst = field->dst,
                                    .src = ValueRef::Number(field_it->second),
                                };
                                if (object_temp != kInvalidLocal) {
                                    temp_types[object_temp] =
                                        *resolved_class_name;
                                }
                                changed = true;
                            }
                        }
                    }
                } else if (auto *method = std::get_if<ResolveMethodSlotInst>(
                               &instruction)) {
                    std::optional<std::string> resolved_class_name =
                        TypeOfValue(method->object, temp_types);

                    if (!resolved_class_name.has_value()) {
                        std::optional<std::string> unique_owner;
                        for (const auto &[candidate_name, candidate_class] :
                             class_by_name_) {
                            if (candidate_class->method_slots.contains(
                                    method->method)) {
                                if (unique_owner.has_value()) {
                                    unique_owner.reset();
                                    break;
                                }
                                unique_owner = candidate_name;
                            }
                        }
                        resolved_class_name = unique_owner;
                    }

                    if (resolved_class_name.has_value()) {
                        const auto class_it =
                            class_by_name_.find(*resolved_class_name);
                        if (class_it != class_by_name_.end()) {
                            const auto slot_it =
                                class_it->second->method_slots.find(
                                    method->method);
                            if (slot_it !=
                                class_it->second->method_slots.end()) {
                                const LocalId object_temp =
                                    method->object.kind == ValueKind::Temp
                                        ? method->object.temp
                                        : kInvalidLocal;
                                instruction = MoveInst{
                                    .dst = method->dst,
                                    .src = ValueRef::Number(slot_it->second),
                                };
                                if (object_temp != kInvalidLocal) {
                                    temp_types[object_temp] =
                                        *resolved_class_name;
                                }
                                changed = true;
                            }
                        }
                    }
                }

                const std::optional<LocalId> dst =
                    passes::detail::InstructionDestination(instruction);
                if (dst.has_value()) {
                    temp_types.erase(*dst);
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
                                const auto source_it =
                                    temp_types.find(inst.src.temp);
                                if (source_it != temp_types.end()) {
                                    temp_types[inst.dst] = source_it->second;
                                }
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
                        } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                            const std::optional<std::string> object_type =
                                TypeOfValue(inst.object, temp_types);
                            if (!object_type.has_value()) {
                                return;
                            }
                            const auto class_it = class_by_name_.find(*object_type);
                            if (class_it == class_by_name_.end()) {
                                return;
                            }
                            SlotId offset = kInvalidSlot;
                            if (!TryIntegralNumber(inst.offset, offset)) {
                                return;
                            }
                            for (std::size_t field_index = 0;
                                 field_index < class_it->second->fields.size();
                                 ++field_index) {
                                const std::string &field_name =
                                    class_it->second->fields[field_index];
                                const auto field_offset_it =
                                    class_it->second->field_offsets.find(field_name);
                                if (field_offset_it ==
                                        class_it->second->field_offsets.end() ||
                                    field_offset_it->second != offset) {
                                    continue;
                                }
                                if (field_index <
                                        class_it->second->field_types.size() &&
                                    !class_it->second->field_types[field_index]
                                         .empty()) {
                                    temp_types[inst.dst] =
                                        class_it->second->field_types[field_index];
                                }
                                break;
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

std::unique_ptr<Pass> CreateResolveObjectAccessPass() {
    return std::make_unique<ResolveObjectAccessPass>();
}

} // namespace compiler::ir
