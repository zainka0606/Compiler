#include "PassFactories.h"
#include "PassUtils.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace compiler::bytecode {

namespace {

using LiveSet = std::unordered_set<RegisterId>;

std::optional<RegisterId> DefRegister(const Instruction &instruction) {
    if (instruction.dst != kInvalidRegister) {
        return instruction.dst;
    }
    return std::nullopt;
}

void CollectOperandRegisters(const Operand &operand,
                             std::vector<RegisterId> &out) {
    if (operand.kind == OperandKind::Register &&
        operand.reg != kInvalidRegister) {
        out.push_back(operand.reg);
    }
}

std::vector<RegisterId> UseRegisters(const Instruction &instruction) {
    std::vector<RegisterId> out;
    CollectOperandRegisters(instruction.a, out);
    CollectOperandRegisters(instruction.b, out);
    CollectOperandRegisters(instruction.c, out);
    for (const Operand &operand : instruction.operands) {
        CollectOperandRegisters(operand, out);
    }
    return out;
}

struct CFGInfo {
    std::vector<std::vector<Address>> succ;
};

CFGInfo BuildCFG(const Function &function) {
    CFGInfo cfg;
    cfg.succ.resize(function.code.size());
    for (Address ip = 0; ip < function.code.size(); ++ip) {
        const Instruction &inst = function.code[ip];
        if (inst.opcode == OpCode::Return) {
            continue;
        }
        if (inst.opcode == OpCode::Jump) {
            cfg.succ[ip].push_back(inst.target);
            continue;
        }
        if (IsConditionalJumpOpcode(inst.opcode)) {
            cfg.succ[ip].push_back(inst.target);
            if (ip + 1 < function.code.size()) {
                cfg.succ[ip].push_back(ip + 1);
            }
            continue;
        }
        if (ip + 1 < function.code.size()) {
            cfg.succ[ip].push_back(ip + 1);
        }
    }
    return cfg;
}

struct Liveness {
    std::vector<LiveSet> in;
    std::vector<LiveSet> out;
};

Liveness ComputeLiveness(const Function &function) {
    Liveness live;
    live.in.resize(function.code.size());
    live.out.resize(function.code.size());

    const CFGInfo cfg = BuildCFG(function);
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = function.code.size(); i-- > 0;) {
            LiveSet new_out;
            for (const Address succ : cfg.succ[i]) {
                if (succ < live.in.size()) {
                    new_out.insert(live.in[succ].begin(), live.in[succ].end());
                }
            }

            LiveSet new_in = new_out;
            if (const std::optional<RegisterId> def = DefRegister(function.code[i]);
                def.has_value()) {
                new_in.erase(*def);
            }
            for (const RegisterId use : UseRegisters(function.code[i])) {
                new_in.insert(use);
            }

            if (new_out != live.out[i] || new_in != live.in[i]) {
                changed = true;
                live.out[i] = std::move(new_out);
                live.in[i] = std::move(new_in);
            }
        }
    }
    return live;
}

struct InterferenceGraph {
    std::unordered_map<RegisterId, std::unordered_set<RegisterId>> edges;
    std::unordered_map<RegisterId, std::size_t> uses;
};

void AddInterferenceEdge(InterferenceGraph &graph, const RegisterId a,
                         const RegisterId b) {
    if (a == b) {
        return;
    }
    graph.edges[a].insert(b);
    graph.edges[b].insert(a);
}

InterferenceGraph BuildInterferenceGraph(const Function &function,
                                         const Liveness &live) {
    InterferenceGraph graph;
    for (std::size_t ip = 0; ip < function.code.size(); ++ip) {
        const Instruction &inst = function.code[ip];
        for (const RegisterId use : UseRegisters(inst)) {
            ++graph.uses[use];
            graph.edges.try_emplace(use);
        }

        const std::optional<RegisterId> def = DefRegister(inst);
        if (def.has_value()) {
            graph.edges.try_emplace(*def);
            ++graph.uses[*def];
            for (const RegisterId live_out : live.out[ip]) {
                AddInterferenceEdge(graph, *def, live_out);
            }
        }
    }
    return graph;
}

struct RegisterAllocation {
    std::unordered_map<RegisterId, RegisterId> color_by_vreg;
    std::unordered_map<RegisterId, SlotId> spill_slot_by_vreg;
    SlotId spill_count = 0;
};

RegisterAllocation AllocateRegistersChaitinBriggs(const Function &function) {
    const Liveness live = ComputeLiveness(function);
    const InterferenceGraph graph = BuildInterferenceGraph(function, live);

    constexpr RegisterId kColorCount = kAllocatableRegisterCount;
    std::unordered_map<RegisterId, std::unordered_set<RegisterId>> work_graph =
        graph.edges;
    std::vector<RegisterId> simplify_stack;
    simplify_stack.reserve(work_graph.size());

    while (!work_graph.empty()) {
        std::optional<RegisterId> low_degree;
        for (const auto &[node, neighbors] : work_graph) {
            if (neighbors.size() < kColorCount) {
                low_degree = node;
                break;
            }
        }

        RegisterId selected = 0;
        if (low_degree.has_value()) {
            selected = *low_degree;
        } else {
            const auto spill_it = std::max_element(
                work_graph.begin(), work_graph.end(),
                [&](const auto &lhs, const auto &rhs) {
                    const std::size_t lhs_degree = lhs.second.size();
                    const std::size_t rhs_degree = rhs.second.size();
                    const std::size_t lhs_uses = graph.uses.contains(lhs.first)
                                                     ? graph.uses.at(lhs.first)
                                                     : 1;
                    const std::size_t rhs_uses = graph.uses.contains(rhs.first)
                                                     ? graph.uses.at(rhs.first)
                                                     : 1;
                    const double lhs_score =
                        static_cast<double>(lhs_degree) /
                        static_cast<double>(std::max<std::size_t>(1, lhs_uses));
                    const double rhs_score =
                        static_cast<double>(rhs_degree) /
                        static_cast<double>(std::max<std::size_t>(1, rhs_uses));
                    return lhs_score < rhs_score;
                });
            selected = spill_it->first;
        }

        simplify_stack.push_back(selected);
        const std::unordered_set<RegisterId> neighbors = work_graph[selected];
        work_graph.erase(selected);
        for (const RegisterId neighbor : neighbors) {
            auto it = work_graph.find(neighbor);
            if (it != work_graph.end()) {
                it->second.erase(selected);
            }
        }
    }

    RegisterAllocation allocation;
    while (!simplify_stack.empty()) {
        const RegisterId node = simplify_stack.back();
        simplify_stack.pop_back();

        std::array<bool, kColorCount> used{};
        const auto neighbors_it = graph.edges.find(node);
        if (neighbors_it != graph.edges.end()) {
            for (const RegisterId neighbor : neighbors_it->second) {
                const auto color_it = allocation.color_by_vreg.find(neighbor);
                if (color_it != allocation.color_by_vreg.end() &&
                    color_it->second < kColorCount) {
                    used[color_it->second] = true;
                }
            }
        }

        std::optional<RegisterId> color;
        for (RegisterId c = 0; c < kColorCount; ++c) {
            if (!used[c]) {
                color = c;
                break;
            }
        }

        if (color.has_value()) {
            allocation.color_by_vreg[node] = *color;
            continue;
        }

        allocation.spill_slot_by_vreg[node] = allocation.spill_count++;
    }

    return allocation;
}

void RewriteOperandForAllocation(Operand &operand,
                                 const RegisterAllocation &allocation,
                                 const SlotId spill_base) {
    if (operand.kind != OperandKind::Register ||
        operand.reg == kInvalidRegister) {
        return;
    }

    const auto color_it = allocation.color_by_vreg.find(operand.reg);
    if (color_it != allocation.color_by_vreg.end()) {
        operand.reg = color_it->second;
        return;
    }

    const auto spill_it = allocation.spill_slot_by_vreg.find(operand.reg);
    if (spill_it == allocation.spill_slot_by_vreg.end()) {
        throw BytecodeException("internal register allocation error: unmapped "
                                "vreg r" +
                                std::to_string(operand.reg));
    }
    operand = Operand::StackSlot(spill_base + spill_it->second);
}

class RegisterAllocationPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "RegisterAllocationPass";
    }

    [[nodiscard]] bool RunOnce() const override { return true; }

    bool Run(Function &function) override {
        const RegisterAllocation allocation =
            AllocateRegistersChaitinBriggs(function);
        const SlotId spill_base = function.stack_slot_count;
        function.stack_slot_count += allocation.spill_count;
        const RegisterId previous_register_count = function.register_count;
        function.register_count = kRegisterCount;
        bool changed = previous_register_count != function.register_count ||
                       allocation.spill_count != 0;

        for (Instruction &instruction : function.code) {
            const Operand before_a = instruction.a;
            const Operand before_b = instruction.b;
            const Operand before_c = instruction.c;
            RewriteOperandForAllocation(instruction.a, allocation, spill_base);
            RewriteOperandForAllocation(instruction.b, allocation, spill_base);
            RewriteOperandForAllocation(instruction.c, allocation, spill_base);
            changed |= before_a.kind != instruction.a.kind ||
                       before_a.reg != instruction.a.reg ||
                       before_a.stack_slot != instruction.a.stack_slot;
            changed |= before_b.kind != instruction.b.kind ||
                       before_b.reg != instruction.b.reg ||
                       before_b.stack_slot != instruction.b.stack_slot;
            changed |= before_c.kind != instruction.c.kind ||
                       before_c.reg != instruction.c.reg ||
                       before_c.stack_slot != instruction.c.stack_slot;
            for (Operand &operand : instruction.operands) {
                const Operand before = operand;
                RewriteOperandForAllocation(operand, allocation, spill_base);
                changed |= before.kind != operand.kind ||
                           before.reg != operand.reg ||
                           before.stack_slot != operand.stack_slot;
            }

            if (instruction.dst != kInvalidRegister) {
                const RegisterId before_dst = instruction.dst;
                const SlotId before_dst_slot = instruction.dst_slot;
                const auto color_it =
                    allocation.color_by_vreg.find(instruction.dst);
                if (color_it != allocation.color_by_vreg.end()) {
                    instruction.dst = color_it->second;
                    instruction.dst_slot = kInvalidSlot;
                } else {
                    const auto spill_it =
                        allocation.spill_slot_by_vreg.find(instruction.dst);
                    if (spill_it == allocation.spill_slot_by_vreg.end()) {
                        throw BytecodeException("internal register allocation "
                                                "error: unmapped dst vreg r" +
                                                std::to_string(instruction.dst));
                    }
                    instruction.dst = kInvalidRegister;
                    instruction.dst_slot = spill_base + spill_it->second;
                }
                changed |= before_dst != instruction.dst ||
                           before_dst_slot != instruction.dst_slot;
            }
        }

        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> CreateRegisterAllocationPass() {
    return std::make_unique<RegisterAllocationPass>();
}

} // namespace compiler::bytecode
