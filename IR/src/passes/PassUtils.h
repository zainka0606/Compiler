#pragma once

#include "IR.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace compiler::ir::passes::detail {

inline std::string NumberToString(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

inline std::string ImmediateToString(const ValueRef &value) {
    switch (value.kind) {
    case ValueKind::Temp:
        return "%" + std::to_string(value.temp);
    case ValueKind::Number:
        return NumberToString(value.number);
    case ValueKind::String:
        return value.text;
    case ValueKind::Bool:
        return value.boolean ? "true" : "false";
    case ValueKind::Char:
        return std::string(1, value.character);
    case ValueKind::Null:
        return "null";
    }
    return "null";
}

inline bool IsPureInstruction(const Instruction &instruction) {
    return std::visit(
        [](const auto &inst) -> bool {
            using T = std::decay_t<decltype(inst)>;
            return std::is_same_v<T, MoveInst> ||
                   std::is_same_v<T, UnaryInst> ||
                   std::is_same_v<T, BinaryInst> ||
                   std::is_same_v<T, LoadLocalInst> ||
                   std::is_same_v<T, LoadGlobalInst> ||
                   std::is_same_v<T, MakeArrayInst> ||
                   std::is_same_v<T, ArrayLoadInst> ||
                   std::is_same_v<T, ResolveFieldOffsetInst> ||
                   std::is_same_v<T, ObjectLoadInst> ||
                   std::is_same_v<T, ResolveMethodSlotInst>;
        },
        instruction);
}

inline std::optional<LocalId>
InstructionDestination(const Instruction &instruction) {
    return std::visit(
        [](const auto &inst) -> std::optional<LocalId> {
            using T = std::decay_t<decltype(inst)>;
            if constexpr (std::is_same_v<T, LoadLocalInst> ||
                          std::is_same_v<T, LoadGlobalInst> ||
                          std::is_same_v<T, MoveInst> ||
                          std::is_same_v<T, UnaryInst> ||
                          std::is_same_v<T, BinaryInst> ||
                          std::is_same_v<T, MakeArrayInst> ||
                          std::is_same_v<T, ArrayLoadInst> ||
                          std::is_same_v<T, ResolveFieldOffsetInst> ||
                          std::is_same_v<T, ObjectLoadInst> ||
                          std::is_same_v<T, ResolveMethodSlotInst> ||
                          std::is_same_v<T, CallInst> ||
                          std::is_same_v<T, VirtualCallInst>) {
                return inst.dst;
            }
            return std::nullopt;
        },
        instruction);
}

inline void
CollectValueRefUses(const ValueRef &value,
                    std::unordered_map<LocalId, std::size_t> &uses) {
    if (value.kind == ValueKind::Temp) {
        ++uses[value.temp];
    }
}

inline void
CollectInstructionUses(const Instruction &instruction,
                       std::unordered_map<LocalId, std::size_t> &uses) {
    std::visit(
        [&](const auto &inst) {
            using T = std::decay_t<decltype(inst)>;
            if constexpr (std::is_same_v<T, StoreLocalInst>) {
                CollectValueRefUses(inst.value, uses);
            } else if constexpr (std::is_same_v<T, DeclareGlobalInst>) {
                CollectValueRefUses(inst.value, uses);
            } else if constexpr (std::is_same_v<T, StoreGlobalInst>) {
                CollectValueRefUses(inst.value, uses);
            } else if constexpr (std::is_same_v<T, MoveInst>) {
                CollectValueRefUses(inst.src, uses);
            } else if constexpr (std::is_same_v<T, UnaryInst>) {
                CollectValueRefUses(inst.value, uses);
            } else if constexpr (std::is_same_v<T, BinaryInst>) {
                CollectValueRefUses(inst.lhs, uses);
                CollectValueRefUses(inst.rhs, uses);
            } else if constexpr (std::is_same_v<T, MakeArrayInst>) {
                for (const ValueRef &value : inst.elements) {
                    CollectValueRefUses(value, uses);
                }
            } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                CollectValueRefUses(inst.array, uses);
                CollectValueRefUses(inst.index, uses);
            } else if constexpr (std::is_same_v<T, ArrayStoreInst>) {
                CollectValueRefUses(inst.array, uses);
                CollectValueRefUses(inst.index, uses);
                CollectValueRefUses(inst.value, uses);
            } else if constexpr (std::is_same_v<T, ResolveFieldOffsetInst>) {
                CollectValueRefUses(inst.object, uses);
            } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                CollectValueRefUses(inst.object, uses);
                CollectValueRefUses(inst.offset, uses);
            } else if constexpr (std::is_same_v<T, ObjectStoreInst>) {
                CollectValueRefUses(inst.object, uses);
                CollectValueRefUses(inst.offset, uses);
                CollectValueRefUses(inst.value, uses);
            } else if constexpr (std::is_same_v<T, ResolveMethodSlotInst>) {
                CollectValueRefUses(inst.object, uses);
            } else if constexpr (std::is_same_v<T, CallInst>) {
                for (const ValueRef &value : inst.args) {
                    CollectValueRefUses(value, uses);
                }
            } else if constexpr (std::is_same_v<T, VirtualCallInst>) {
                CollectValueRefUses(inst.object, uses);
                CollectValueRefUses(inst.slot, uses);
                for (const ValueRef &value : inst.args) {
                    CollectValueRefUses(value, uses);
                }
            }
        },
        instruction);
}

inline void
CollectTerminatorUses(const Terminator &terminator,
                      std::unordered_map<LocalId, std::size_t> &uses) {
    std::visit(
        [&](const auto &term) {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, BranchTerm>) {
                CollectValueRefUses(term.condition, uses);
            } else if constexpr (std::is_same_v<T, ReturnTerm>) {
                if (term.value.has_value()) {
                    CollectValueRefUses(*term.value, uses);
                }
            }
        },
        terminator);
}

inline ValueRef
ResolveValueRef(const ValueRef &value,
                const std::unordered_map<LocalId, ValueRef> &copies) {
    if (value.kind != ValueKind::Temp) {
        return value;
    }

    ValueRef current = value;
    std::unordered_set<LocalId> seen;
    while (current.kind == ValueKind::Temp) {
        if (!seen.insert(current.temp).second) {
            break;
        }
        const auto it = copies.find(current.temp);
        if (it == copies.end()) {
            break;
        }
        current = it->second;
    }
    return current;
}

inline std::optional<ValueRef> TryFoldUnary(UnaryOp op, const ValueRef &value) {
    if (!value.IsImmediate()) {
        return std::nullopt;
    }

    if (op == UnaryOp::Negate && value.kind == ValueKind::Number) {
        return ValueRef::Number(-value.number);
    }
    if (op == UnaryOp::LogicalNot) {
        bool truthy = false;
        switch (value.kind) {
        case ValueKind::Number:
            truthy = value.number != 0.0;
            break;
        case ValueKind::String:
            truthy = !value.text.empty();
            break;
        case ValueKind::Bool:
            truthy = value.boolean;
            break;
        case ValueKind::Char:
            truthy = value.character != '\0';
            break;
        case ValueKind::Null:
            truthy = false;
            break;
        case ValueKind::Temp:
            return std::nullopt;
        }
        return ValueRef::Bool(!truthy);
    }
    if (op == UnaryOp::BitwiseNot && value.kind == ValueKind::Number) {
        const std::int64_t iv = static_cast<std::int64_t>(value.number);
        return ValueRef::Number(static_cast<double>(~iv));
    }

    return std::nullopt;
}

inline std::optional<ValueRef> TryFoldBinary(BinaryOp op, const ValueRef &lhs,
                                             const ValueRef &rhs) {
    if (!lhs.IsImmediate() || !rhs.IsImmediate()) {
        return std::nullopt;
    }

    const auto as_number = [](const ValueRef &value,
                              std::optional<double> &out) -> bool {
        if (value.kind == ValueKind::Number) {
            out = value.number;
            return true;
        }
        if (value.kind == ValueKind::Bool) {
            out = value.boolean ? 1.0 : 0.0;
            return true;
        }
        if (value.kind == ValueKind::Char) {
            out = static_cast<double>(
                static_cast<unsigned char>(value.character));
            return true;
        }
        return false;
    };

    std::optional<double> left_number;
    std::optional<double> right_number;
    const bool left_numeric = as_number(lhs, left_number);
    const bool right_numeric = as_number(rhs, right_number);

    switch (op) {
    case BinaryOp::Add:
        if (lhs.kind == ValueKind::String || rhs.kind == ValueKind::String) {
            return ValueRef::String(ImmediateToString(lhs) +
                                    ImmediateToString(rhs));
        }
        if (left_numeric && right_numeric) {
            return ValueRef::Number(*left_number + *right_number);
        }
        break;
    case BinaryOp::Subtract:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(*left_number - *right_number);
        }
        break;
    case BinaryOp::Multiply:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(*left_number * *right_number);
        }
        break;
    case BinaryOp::Divide:
        if (left_numeric && right_numeric && *right_number != 0.0) {
            return ValueRef::Number(*left_number / *right_number);
        }
        break;
    case BinaryOp::IntDivide:
        if (left_numeric && right_numeric && *right_number != 0.0) {
            return ValueRef::Number(std::floor(*left_number / *right_number));
        }
        break;
    case BinaryOp::Modulo:
        if (left_numeric && right_numeric && *right_number != 0.0) {
            return ValueRef::Number(std::fmod(*left_number, *right_number));
        }
        break;
    case BinaryOp::Pow:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(std::pow(*left_number, *right_number));
        }
        break;
    case BinaryOp::BitwiseAnd:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(
                static_cast<double>(static_cast<std::int64_t>(*left_number) &
                                    static_cast<std::int64_t>(*right_number)));
        }
        break;
    case BinaryOp::BitwiseOr:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(
                static_cast<double>(static_cast<std::int64_t>(*left_number) |
                                    static_cast<std::int64_t>(*right_number)));
        }
        break;
    case BinaryOp::BitwiseXor:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(
                static_cast<double>(static_cast<std::int64_t>(*left_number) ^
                                    static_cast<std::int64_t>(*right_number)));
        }
        break;
    case BinaryOp::ShiftLeft:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(static_cast<double>(
                static_cast<std::int64_t>(*left_number)
                << static_cast<std::int64_t>(*right_number)));
        }
        break;
    case BinaryOp::ShiftRight:
        if (left_numeric && right_numeric) {
            return ValueRef::Number(
                static_cast<double>(static_cast<std::int64_t>(*left_number) >>
                                    static_cast<std::int64_t>(*right_number)));
        }
        break;
    case BinaryOp::Equal:
        if (left_numeric && right_numeric) {
            return ValueRef::Bool(*left_number == *right_number);
        }
        if (lhs.kind == rhs.kind) {
            if (lhs.kind == ValueKind::String) {
                return ValueRef::Bool(lhs.text == rhs.text);
            }
            if (lhs.kind == ValueKind::Bool) {
                return ValueRef::Bool(lhs.boolean == rhs.boolean);
            }
            if (lhs.kind == ValueKind::Char) {
                return ValueRef::Bool(lhs.character == rhs.character);
            }
            if (lhs.kind == ValueKind::Null) {
                return ValueRef::Bool(true);
            }
        }
        break;
    case BinaryOp::NotEqual:
        if (left_numeric && right_numeric) {
            return ValueRef::Bool(*left_number != *right_number);
        }
        break;
    case BinaryOp::Less:
        if (left_numeric && right_numeric) {
            return ValueRef::Bool(*left_number < *right_number);
        }
        break;
    case BinaryOp::LessEqual:
        if (left_numeric && right_numeric) {
            return ValueRef::Bool(*left_number <= *right_number);
        }
        break;
    case BinaryOp::Greater:
        if (left_numeric && right_numeric) {
            return ValueRef::Bool(*left_number > *right_number);
        }
        break;
    case BinaryOp::GreaterEqual:
        if (left_numeric && right_numeric) {
            return ValueRef::Bool(*left_number >= *right_number);
        }
        break;
    }

    return std::nullopt;
}

inline std::optional<bool>
TryEvaluateImmediateTruthiness(const ValueRef &value) {
    if (!value.IsImmediate()) {
        return std::nullopt;
    }
    switch (value.kind) {
    case ValueKind::Temp:
        return std::nullopt;
    case ValueKind::Number:
        return value.number != 0.0;
    case ValueKind::String:
        return !value.text.empty();
    case ValueKind::Bool:
        return value.boolean;
    case ValueKind::Char:
        return value.character != '\0';
    case ValueKind::Null:
        return false;
    }
    return std::nullopt;
}

inline BlockId ThreadJumpTarget(const Function &function, BlockId target) {
    std::unordered_set<BlockId> seen;
    BlockId current = target;
    while (current < function.blocks.size() && seen.insert(current).second) {
        const BasicBlock &block = function.blocks[current];
        if (!block.instructions.empty() || !block.terminator.has_value()) {
            break;
        }
        const auto *jump = std::get_if<JumpTerm>(&*block.terminator);
        if (jump == nullptr) {
            break;
        }
        current = jump->target;
    }
    return current;
}

} // namespace compiler::ir::passes::detail
