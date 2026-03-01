#include "PassFactories.h"

#include "PassUtils.h"

#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace compiler::ir {

namespace {

enum class ExprKind : std::uint8_t {
    Move,
    Unary,
    Binary,
    LoadLocal,
    LoadGlobal,
    ArrayLoad,
    ObjectLoad,
    ResolveFieldOffset,
    ResolveMethodSlot,
};

void HashCombine(std::size_t &seed, const std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

struct ValueKey {
    ValueKind kind = ValueKind::Null;
    LocalId temp = kInvalidLocal;
    std::uint64_t number_bits = 0;
    std::string text;
    bool boolean = false;
    char character = '\0';

    bool operator==(const ValueKey &) const = default;
};

ValueKey ToValueKey(const ValueRef &value) {
    ValueKey key;
    key.kind = value.kind;
    switch (value.kind) {
    case ValueKind::Temp:
        key.temp = value.temp;
        break;
    case ValueKind::Number:
        key.number_bits = std::bit_cast<std::uint64_t>(value.number);
        break;
    case ValueKind::String:
        key.text = value.text;
        break;
    case ValueKind::Bool:
        key.boolean = value.boolean;
        break;
    case ValueKind::Char:
        key.character = value.character;
        break;
    case ValueKind::Null:
        break;
    }
    return key;
}

struct ValueKeyHash {
    std::size_t operator()(const ValueKey &key) const noexcept {
        std::size_t seed = 0;
        HashCombine(seed, std::hash<int>{}(static_cast<int>(key.kind)));
        switch (key.kind) {
        case ValueKind::Temp:
            HashCombine(seed, std::hash<LocalId>{}(key.temp));
            break;
        case ValueKind::Number:
            HashCombine(seed, std::hash<std::uint64_t>{}(key.number_bits));
            break;
        case ValueKind::String:
            HashCombine(seed, std::hash<std::string>{}(key.text));
            break;
        case ValueKind::Bool:
            HashCombine(seed, std::hash<bool>{}(key.boolean));
            break;
        case ValueKind::Char:
            HashCombine(seed, std::hash<char>{}(key.character));
            break;
        case ValueKind::Null:
            break;
        }
        return seed;
    }
};

struct ExprKey {
    ExprKind kind = ExprKind::Move;
    UnaryOp unary_op = UnaryOp::Negate;
    BinaryOp binary_op = BinaryOp::Add;
    SlotId slot = kInvalidSlot;
    std::string text;
    std::vector<ValueKey> args;
    std::uint64_t local_version = 0;
    std::uint64_t global_epoch = 0;
    std::uint64_t memory_epoch = 0;

    bool operator==(const ExprKey &) const = default;
};

struct ExprKeyHash {
    std::size_t operator()(const ExprKey &key) const noexcept {
        std::size_t seed = 0;
        HashCombine(seed, std::hash<int>{}(static_cast<int>(key.kind)));
        HashCombine(seed, std::hash<int>{}(static_cast<int>(key.unary_op)));
        HashCombine(seed, std::hash<int>{}(static_cast<int>(key.binary_op)));
        HashCombine(seed, std::hash<SlotId>{}(key.slot));
        HashCombine(seed, std::hash<std::string>{}(key.text));
        HashCombine(seed, std::hash<std::uint64_t>{}(key.local_version));
        HashCombine(seed, std::hash<std::uint64_t>{}(key.global_epoch));
        HashCombine(seed, std::hash<std::uint64_t>{}(key.memory_epoch));
        for (const ValueKey &arg : key.args) {
            HashCombine(seed, ValueKeyHash{}(arg));
        }
        return seed;
    }
};

bool SameValueRef(const ValueRef &lhs, const ValueRef &rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }
    switch (lhs.kind) {
    case ValueKind::Temp:
        return lhs.temp == rhs.temp;
    case ValueKind::Number:
        return std::bit_cast<std::uint64_t>(lhs.number) ==
               std::bit_cast<std::uint64_t>(rhs.number);
    case ValueKind::String:
        return lhs.text == rhs.text;
    case ValueKind::Bool:
        return lhs.boolean == rhs.boolean;
    case ValueKind::Char:
        return lhs.character == rhs.character;
    case ValueKind::Null:
        return true;
    }
    return false;
}

class LocalExpressionDAG {
  public:
    [[nodiscard]] ValueRef Resolve(const ValueRef &value) const {
        return passes::detail::ResolveValueRef(value, copies_);
    }

    bool Rewrite(ValueRef &value) const {
        const ValueRef rewritten = Resolve(value);
        if (SameValueRef(value, rewritten)) {
            return false;
        }
        value = rewritten;
        return true;
    }

    [[nodiscard]] std::optional<LocalId> Lookup(const ExprKey &key) const {
        const auto it = expressions_.find(key);
        if (it == expressions_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void Record(ExprKey key, const LocalId temp) {
        expressions_[std::move(key)] = temp;
    }

    void KillDestination(const LocalId dst) { copies_.erase(dst); }

    void BindCopy(const LocalId dst, ValueRef src) { copies_[dst] = Resolve(src); }

    [[nodiscard]] std::uint64_t LocalVersion(const SlotId slot) const {
        const auto it = local_versions_.find(slot);
        if (it == local_versions_.end()) {
            return 0;
        }
        return it->second;
    }

    void BumpLocalVersion(const SlotId slot) { ++local_versions_[slot]; }

    [[nodiscard]] std::uint64_t GlobalEpoch() const { return global_epoch_; }

    void BumpGlobalEpoch() { ++global_epoch_; }

    [[nodiscard]] std::uint64_t MemoryEpoch() const { return memory_epoch_; }

    void BumpMemoryEpoch() { ++memory_epoch_; }

  private:
    std::unordered_map<LocalId, ValueRef> copies_;
    std::unordered_map<ExprKey, LocalId, ExprKeyHash> expressions_;
    std::unordered_map<SlotId, std::uint64_t> local_versions_;
    std::uint64_t global_epoch_ = 0;
    std::uint64_t memory_epoch_ = 0;
};

bool RewriteInstructionOperands(Instruction &instruction,
                                const LocalExpressionDAG &dag) {
    return std::visit(
        [&](auto &inst) -> bool {
            using T = std::decay_t<decltype(inst)>;
            bool changed = false;
            if constexpr (std::is_same_v<T, StoreLocalInst>) {
                changed |= dag.Rewrite(inst.value);
            } else if constexpr (std::is_same_v<T, DeclareGlobalInst>) {
                changed |= dag.Rewrite(inst.value);
            } else if constexpr (std::is_same_v<T, StoreGlobalInst>) {
                changed |= dag.Rewrite(inst.value);
            } else if constexpr (std::is_same_v<T, MoveInst>) {
                changed |= dag.Rewrite(inst.src);
            } else if constexpr (std::is_same_v<T, UnaryInst>) {
                changed |= dag.Rewrite(inst.value);
            } else if constexpr (std::is_same_v<T, BinaryInst>) {
                changed |= dag.Rewrite(inst.lhs);
                changed |= dag.Rewrite(inst.rhs);
            } else if constexpr (std::is_same_v<T, MakeArrayInst>) {
                for (ValueRef &value : inst.elements) {
                    changed |= dag.Rewrite(value);
                }
            } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                changed |= dag.Rewrite(inst.array);
                changed |= dag.Rewrite(inst.index);
            } else if constexpr (std::is_same_v<T, ArrayStoreInst>) {
                changed |= dag.Rewrite(inst.array);
                changed |= dag.Rewrite(inst.index);
                changed |= dag.Rewrite(inst.value);
            } else if constexpr (std::is_same_v<T, ResolveFieldOffsetInst>) {
                changed |= dag.Rewrite(inst.object);
            } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                changed |= dag.Rewrite(inst.object);
                changed |= dag.Rewrite(inst.offset);
            } else if constexpr (std::is_same_v<T, ObjectStoreInst>) {
                changed |= dag.Rewrite(inst.object);
                changed |= dag.Rewrite(inst.offset);
                changed |= dag.Rewrite(inst.value);
            } else if constexpr (std::is_same_v<T, ResolveMethodSlotInst>) {
                changed |= dag.Rewrite(inst.object);
            } else if constexpr (std::is_same_v<T, CallInst>) {
                for (ValueRef &value : inst.args) {
                    changed |= dag.Rewrite(value);
                }
            } else if constexpr (std::is_same_v<T, VirtualCallInst>) {
                changed |= dag.Rewrite(inst.object);
                changed |= dag.Rewrite(inst.slot);
                for (ValueRef &value : inst.args) {
                    changed |= dag.Rewrite(value);
                }
            }
            return changed;
        },
        instruction);
}

bool RewriteTerminatorOperands(Terminator &terminator,
                               const LocalExpressionDAG &dag) {
    return std::visit(
        [&](auto &term) -> bool {
            using T = std::decay_t<decltype(term)>;
            bool changed = false;
            if constexpr (std::is_same_v<T, BranchTerm>) {
                changed |= dag.Rewrite(term.condition);
            } else if constexpr (std::is_same_v<T, ReturnTerm>) {
                if (term.value.has_value()) {
                    changed |= dag.Rewrite(*term.value);
                }
            }
            return changed;
        },
        terminator);
}

std::optional<ExprKey> BuildExpressionKey(const Instruction &instruction,
                                          const LocalExpressionDAG &dag) {
    return std::visit(
        [&](const auto &inst) -> std::optional<ExprKey> {
            using T = std::decay_t<decltype(inst)>;

            if constexpr (std::is_same_v<T, MoveInst>) {
                ExprKey key;
                key.kind = ExprKind::Move;
                key.args.push_back(ToValueKey(inst.src));
                return key;
            } else if constexpr (std::is_same_v<T, UnaryInst>) {
                ExprKey key;
                key.kind = ExprKind::Unary;
                key.unary_op = inst.op;
                key.args.push_back(ToValueKey(inst.value));
                return key;
            } else if constexpr (std::is_same_v<T, BinaryInst>) {
                ExprKey key;
                key.kind = ExprKind::Binary;
                key.binary_op = inst.op;
                key.args.push_back(ToValueKey(inst.lhs));
                key.args.push_back(ToValueKey(inst.rhs));
                return key;
            } else if constexpr (std::is_same_v<T, LoadLocalInst>) {
                ExprKey key;
                key.kind = ExprKind::LoadLocal;
                key.slot = inst.slot;
                key.local_version = dag.LocalVersion(inst.slot);
                return key;
            } else if constexpr (std::is_same_v<T, LoadGlobalInst>) {
                ExprKey key;
                key.kind = ExprKind::LoadGlobal;
                key.text = inst.name;
                key.global_epoch = dag.GlobalEpoch();
                return key;
            } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                ExprKey key;
                key.kind = ExprKind::ArrayLoad;
                key.args.push_back(ToValueKey(inst.array));
                key.args.push_back(ToValueKey(inst.index));
                key.memory_epoch = dag.MemoryEpoch();
                return key;
            } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                ExprKey key;
                key.kind = ExprKind::ObjectLoad;
                key.args.push_back(ToValueKey(inst.object));
                key.args.push_back(ToValueKey(inst.offset));
                key.memory_epoch = dag.MemoryEpoch();
                return key;
            } else if constexpr (std::is_same_v<T, ResolveFieldOffsetInst>) {
                ExprKey key;
                key.kind = ExprKind::ResolveFieldOffset;
                key.text = inst.member;
                key.args.push_back(ToValueKey(inst.object));
                return key;
            } else if constexpr (std::is_same_v<T, ResolveMethodSlotInst>) {
                ExprKey key;
                key.kind = ExprKind::ResolveMethodSlot;
                key.text = inst.method;
                key.args.push_back(ToValueKey(inst.object));
                return key;
            }

            return std::nullopt;
        },
        instruction);
}

void ApplySideEffects(const Instruction &instruction, LocalExpressionDAG &dag) {
    std::visit(
        [&](const auto &inst) {
            using T = std::decay_t<decltype(inst)>;
            if constexpr (std::is_same_v<T, StoreLocalInst>) {
                dag.BumpLocalVersion(inst.slot);
            } else if constexpr (std::is_same_v<T, DeclareGlobalInst> ||
                                 std::is_same_v<T, StoreGlobalInst>) {
                dag.BumpGlobalEpoch();
            } else if constexpr (std::is_same_v<T, ArrayStoreInst> ||
                                 std::is_same_v<T, ObjectStoreInst>) {
                dag.BumpMemoryEpoch();
            } else if constexpr (std::is_same_v<T, CallInst> ||
                                 std::is_same_v<T, VirtualCallInst>) {
                dag.BumpGlobalEpoch();
                dag.BumpMemoryEpoch();
            }
        },
        instruction);
}

class CommonSubexpressionEliminationPass : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "CommonSubexpressionEliminationPass";
    }

    bool Run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            changed |= RunOnFunction(function);
        }
        return changed;
    }

  private:
    static bool RunOnFunction(Function &function) {
        bool changed = false;
        for (BasicBlock &block : function.blocks) {
            LocalExpressionDAG dag;
            for (Instruction &instruction : block.instructions) {
                changed |= RewriteInstructionOperands(instruction, dag);

                const std::optional<LocalId> dst =
                    passes::detail::InstructionDestination(instruction);
                if (dst.has_value()) {
                    dag.KillDestination(*dst);
                }

                bool replaced_with_move = false;
                if (dst.has_value()) {
                    if (const std::optional<ExprKey> key =
                            BuildExpressionKey(instruction, dag);
                        key.has_value()) {
                        if (const std::optional<LocalId> existing =
                                dag.Lookup(*key);
                            existing.has_value() && *existing != *dst) {
                            instruction = MoveInst{
                                .dst = *dst,
                                .src = ValueRef::Temp(*existing),
                            };
                            dag.BindCopy(*dst, ValueRef::Temp(*existing));
                            replaced_with_move = true;
                            changed = true;
                        } else {
                            dag.Record(*key, *dst);
                        }
                    }
                }

                if (dst.has_value() && !replaced_with_move) {
                    if (const auto *move =
                            std::get_if<MoveInst>(&instruction)) {
                        dag.BindCopy(move->dst, move->src);
                    }
                }

                ApplySideEffects(instruction, dag);
            }

            if (block.terminator.has_value()) {
                changed |= RewriteTerminatorOperands(*block.terminator, dag);
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateCommonSubexpressionEliminationPass() {
    return std::make_unique<CommonSubexpressionEliminationPass>();
}

} // namespace compiler::ir
