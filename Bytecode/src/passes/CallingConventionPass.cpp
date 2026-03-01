#include "PassFactories.h"
#include "PassUtils.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace compiler::bytecode {

namespace {

bool OperandIsRegister(const Operand &operand, const RegisterId reg) {
    return operand.kind == OperandKind::Register && operand.reg == reg;
}

void EmitMoveToRegister(const RegisterId dst, const Operand &source,
                        std::vector<Instruction> &out) {
    Instruction move;
    move.opcode = OpCode::Move;
    move.dst = dst;
    move.a = source;
    out.push_back(std::move(move));
}

void EmitRegisterArgumentSetup(const Instruction &call_inst,
                               std::vector<Instruction> &out) {
    const std::size_t arg_count = call_inst.operands.size();
    const std::size_t reg_count =
        std::min<std::size_t>(arg_count, kArgRegisterCount);
    if (reg_count == 0) {
        return;
    }

    // Parallel-copy resolve register-to-register argument moves among
    // argument registers. Fall back to a push/pop cycle break only when
    // there is a true cycle.
    std::vector<std::optional<RegisterId>> pending_sources(reg_count);
    std::size_t pending_count = 0;
    for (std::size_t i = 0; i < reg_count; ++i) {
        const Operand &operand = call_inst.operands[i];
        if (operand.kind == OperandKind::Register &&
            operand.reg < reg_count &&
            operand.reg != static_cast<RegisterId>(i)) {
            pending_sources[i] = operand.reg;
            ++pending_count;
        }
    }

    while (pending_count > 0) {
        std::unordered_set<RegisterId> active_sources;
        active_sources.reserve(pending_count);
        for (std::size_t i = 0; i < reg_count; ++i) {
            if (pending_sources[i].has_value()) {
                active_sources.insert(*pending_sources[i]);
            }
        }

        bool progressed = false;
        for (std::size_t i = 0; i < reg_count; ++i) {
            if (!pending_sources[i].has_value()) {
                continue;
            }
            const RegisterId dst = static_cast<RegisterId>(i);
            if (active_sources.contains(dst)) {
                continue;
            }
            EmitMoveToRegister(dst, Operand::Register(*pending_sources[i]), out);
            pending_sources[i].reset();
            --pending_count;
            progressed = true;
        }
        if (progressed) {
            continue;
        }

        std::optional<RegisterId> cycle_start;
        for (std::size_t i = 0; i < reg_count; ++i) {
            if (pending_sources[i].has_value()) {
                cycle_start = static_cast<RegisterId>(i);
                break;
            }
        }
        if (!cycle_start.has_value()) {
            break;
        }

        Instruction save;
        save.opcode = OpCode::Push;
        save.a = Operand::Register(*cycle_start);
        out.push_back(std::move(save));

        RegisterId cursor = *cycle_start;
        while (true) {
            const std::optional<RegisterId> source =
                pending_sources[cursor];
            if (!source.has_value()) {
                throw BytecodeException(
                    "internal error while resolving call argument cycle");
            }
            if (*source == *cycle_start) {
                Instruction restore;
                restore.opcode = OpCode::Pop;
                restore.dst = cursor;
                out.push_back(std::move(restore));
                pending_sources[cursor].reset();
                --pending_count;
                break;
            }
            EmitMoveToRegister(cursor, Operand::Register(*source), out);
            pending_sources[cursor].reset();
            --pending_count;
            cursor = *source;
            if (cursor >= reg_count) {
                throw BytecodeException(
                    "internal error: invalid register in call argument cycle");
            }
        }
    }

    for (std::size_t i = 0; i < reg_count; ++i) {
        const Operand &operand = call_inst.operands[i];
        if (operand.kind == OperandKind::Register &&
            operand.reg == static_cast<RegisterId>(i)) {
            continue;
        }
        if (operand.kind == OperandKind::Register &&
            operand.reg < reg_count) {
            continue;
        }
        EmitMoveToRegister(static_cast<RegisterId>(i), operand, out);
    }
}

void EmitCallArgumentSetup(const Instruction &call_inst,
                           std::vector<Instruction> &out) {
    const std::size_t arg_count = call_inst.operands.size();
    const std::size_t reg_count =
        std::min<std::size_t>(arg_count, kArgRegisterCount);

    for (std::size_t i = reg_count; i < arg_count; ++i) {
        Instruction arg_push;
        arg_push.opcode = OpCode::Push;
        arg_push.a = call_inst.operands[i];
        out.push_back(std::move(arg_push));
    }
    EmitRegisterArgumentSetup(call_inst, out);
}

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

std::vector<LiveSet> ComputeLiveOut(const std::vector<Instruction> &code) {
    std::vector<std::vector<Address>> successors(code.size());
    for (Address ip = 0; ip < code.size(); ++ip) {
        const Instruction &inst = code[ip];
        if (inst.opcode == OpCode::Return) {
            continue;
        }
        if (inst.opcode == OpCode::Jump) {
            successors[ip].push_back(inst.target);
            continue;
        }
        if (IsConditionalJumpOpcode(inst.opcode)) {
            successors[ip].push_back(inst.target);
            if (ip + 1 < code.size()) {
                successors[ip].push_back(ip + 1);
            }
            continue;
        }
        if (ip + 1 < code.size()) {
            successors[ip].push_back(ip + 1);
        }
    }

    std::vector<LiveSet> live_in(code.size());
    std::vector<LiveSet> live_out(code.size());

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = code.size(); i-- > 0;) {
            LiveSet new_out;
            for (const Address succ : successors[i]) {
                if (succ < live_in.size()) {
                    new_out.insert(live_in[succ].begin(), live_in[succ].end());
                }
            }

            LiveSet new_in = new_out;
            if (const std::optional<RegisterId> def = DefRegister(code[i]);
                def.has_value()) {
                new_in.erase(*def);
            }
            for (const RegisterId use : UseRegisters(code[i])) {
                new_in.insert(use);
            }

            if (new_out != live_out[i] || new_in != live_in[i]) {
                live_out[i] = std::move(new_out);
                live_in[i] = std::move(new_in);
                changed = true;
            }
        }
    }
    return live_out;
}

constexpr RegisterId kCalleeSavedFirst = kArgRegisterCount;
constexpr RegisterId kCalleeSavedLast = kBasePointerRegister - 2;

class CallingConventionPass final : public Pass {
  public:
    [[nodiscard]] std::string_view Name() const override {
        return "CallingConventionPass";
    }

    [[nodiscard]] bool RunOnce() const override { return true; }

    bool Run(Function &function) override {
        if (function.code.empty()) {
            return false;
        }

        const std::vector<Instruction> original = function.code;
        const std::vector<LiveSet> live_out = ComputeLiveOut(original);

        std::vector<RegisterId> saved_callee_regs;
        saved_callee_regs.reserve(kCalleeSavedLast - kCalleeSavedFirst + 1);
        for (RegisterId reg = kCalleeSavedFirst; reg <= kCalleeSavedLast; ++reg) {
            bool written = false;
            for (const Instruction &inst : original) {
                const auto def = DefRegister(inst);
                if (def.has_value() && *def == reg) {
                    written = true;
                    break;
                }
            }
            if (written) {
                saved_callee_regs.push_back(reg);
            }
        }

        std::vector<Instruction> prologue;
        prologue.reserve(function.stack_slot_count + function.param_slots.size() +
                         saved_callee_regs.size() + 10);

        Instruction push_bp;
        push_bp.opcode = OpCode::Push;
        push_bp.a = Operand::Register(kBasePointerRegister);
        prologue.push_back(std::move(push_bp));

        for (const RegisterId reg : saved_callee_regs) {
            Instruction save_reg;
            save_reg.opcode = OpCode::Push;
            save_reg.a = Operand::Register(reg);
            prologue.push_back(std::move(save_reg));
        }

        Instruction set_bp;
        set_bp.opcode = OpCode::Move;
        set_bp.dst = kBasePointerRegister;
        set_bp.a = Operand::Register(kStackPointerRegister);
        prologue.push_back(std::move(set_bp));

        if (function.stack_slot_count > 0) {
            Instruction alloc_locals;
            alloc_locals.opcode = OpCode::Binary;
            alloc_locals.binary_op = ir::BinaryOp::Add;
            alloc_locals.dst = kStackPointerRegister;
            alloc_locals.a = Operand::Register(kStackPointerRegister);
            alloc_locals.b =
                Operand::Number(static_cast<double>(function.stack_slot_count));
            prologue.push_back(std::move(alloc_locals));
        }

        for (std::size_t i = 0; i < function.param_slots.size(); ++i) {
            if (i < kArgRegisterCount) {
                Instruction st_param;
                st_param.opcode = OpCode::Store;
                st_param.store_mode = StoreMode::StackRelative;
                st_param.slot = function.param_slots[i];
                st_param.a = Operand::Register(static_cast<RegisterId>(i));
                prologue.push_back(std::move(st_param));
            } else {
                Instruction ld_param;
                ld_param.opcode = OpCode::Load;
                ld_param.load_mode = LoadMode::StackAbsolute;
                ld_param.slot = static_cast<SlotId>(i - kArgRegisterCount);
                ld_param.dst_slot = function.param_slots[i];
                prologue.push_back(std::move(ld_param));
            }
        }

        if (function.is_method) {
            for (SlotId slot = 0; slot < function.slot_names.size(); ++slot) {
                if (function.slot_names[slot] == "self") {
                    Instruction st_self;
                    st_self.opcode = OpCode::Store;
                    st_self.store_mode = StoreMode::StackRelative;
                    st_self.slot = slot;
                    st_self.a = Operand::Register(kSelfRegister);
                    prologue.push_back(std::move(st_self));
                }
            }
        }

        std::vector<Instruction> lowered;
        lowered.reserve(prologue.size() + original.size() * 5 + 8);
        lowered.insert(lowered.end(), prologue.begin(), prologue.end());
        const std::size_t prologue_jump_index = lowered.size();
        lowered.push_back(Instruction{.opcode = OpCode::Jump});

        std::vector<Address> old_to_new_start(original.size(), 0);
        std::vector<std::pair<std::size_t, Address>> jump_patches;
        std::vector<std::size_t> return_jump_patches;
        const Address old_entry = function.entry_pc;

        for (Address old_pc = 0; old_pc < original.size(); ++old_pc) {
            const Instruction &inst = original[old_pc];
            old_to_new_start[old_pc] = static_cast<Address>(lowered.size());

            if (inst.opcode == OpCode::Call || inst.opcode == OpCode::CallRegister) {
                constexpr RegisterId kCallTargetScratch = kSelfRegister - 1;
                struct VolatileSave {
                    RegisterId reg = kInvalidRegister;
                    RegisterId backup = kInvalidRegister;
                    bool via_stack = true;
                };
                const RegisterId original_dst = inst.dst;
                const SlotId original_dst_slot = inst.dst_slot;
                const std::size_t arg_count = inst.operands.size();
                const std::size_t reg_count =
                    std::min<std::size_t>(arg_count, kArgRegisterCount);
                const std::size_t overflow =
                    arg_count > kArgRegisterCount ? arg_count - kArgRegisterCount
                                                  : 0;
                const bool is_indirect = inst.opcode == OpCode::CallRegister;

                if (is_indirect) {
                    Instruction save_scratch;
                    save_scratch.opcode = OpCode::Push;
                    save_scratch.a = Operand::Register(kCallTargetScratch);
                    lowered.push_back(std::move(save_scratch));

                    Instruction materialize_callee;
                    materialize_callee.opcode = OpCode::Move;
                    materialize_callee.dst = kCallTargetScratch;
                    materialize_callee.a = inst.a;
                    lowered.push_back(std::move(materialize_callee));
                }

                std::unordered_set<RegisterId> call_use_regs;
                for (const RegisterId reg : UseRegisters(inst)) {
                    call_use_regs.insert(reg);
                }

                std::vector<RegisterId> backup_pool;
                backup_pool.reserve(kCalleeSavedLast - kCalleeSavedFirst + 1);
                for (RegisterId reg = kCalleeSavedFirst; reg <= kCalleeSavedLast;
                     ++reg) {
                    if (live_out[old_pc].contains(reg) ||
                        call_use_regs.contains(reg)) {
                        continue;
                    }
                    if (is_indirect && reg == kCallTargetScratch) {
                        continue;
                    }
                    if (original_dst_slot == kInvalidSlot && original_dst == reg) {
                        continue;
                    }
                    backup_pool.push_back(reg);
                }

                std::vector<VolatileSave> saved_volatile_regs;
                const RegisterId clobbered_limit =
                    static_cast<RegisterId>(std::max<std::size_t>(1, reg_count));
                for (RegisterId reg = 0; reg < clobbered_limit; ++reg) {
                    if (!live_out[old_pc].contains(reg)) {
                        continue;
                    }
                    if (original_dst == reg && original_dst_slot == kInvalidSlot) {
                        continue;
                    }

                    VolatileSave save;
                    save.reg = reg;
                    if (!backup_pool.empty()) {
                        save.via_stack = false;
                        save.backup = backup_pool.back();
                        backup_pool.pop_back();
                        EmitMoveToRegister(save.backup, Operand::Register(reg),
                                           lowered);
                    } else {
                        Instruction save_volatile;
                        save_volatile.opcode = OpCode::Push;
                        save_volatile.a = Operand::Register(reg);
                        lowered.push_back(std::move(save_volatile));
                    }
                    saved_volatile_regs.push_back(save);
                }

                EmitCallArgumentSetup(inst, lowered);

                Instruction call = inst;
                call.slot = static_cast<SlotId>(arg_count);
                call.operands.clear();
                call.dst = 0;
                call.dst_slot = kInvalidSlot;
                if (is_indirect) {
                    call.a = Operand::Register(kCallTargetScratch);
                }
                lowered.push_back(std::move(call));

                if (overflow > 0) {
                    Instruction unwind_args;
                    unwind_args.opcode = OpCode::Binary;
                    unwind_args.binary_op = ir::BinaryOp::Subtract;
                    unwind_args.dst = kStackPointerRegister;
                    unwind_args.a = Operand::Register(kStackPointerRegister);
                    unwind_args.b = Operand::Number(static_cast<double>(overflow));
                    lowered.push_back(std::move(unwind_args));
                }

                if (!(original_dst == 0 && original_dst_slot == kInvalidSlot)) {
                    Instruction ret_move;
                    ret_move.opcode = OpCode::Move;
                    ret_move.dst = original_dst;
                    ret_move.dst_slot = original_dst_slot;
                    ret_move.a = Operand::Register(0);
                    lowered.push_back(std::move(ret_move));
                }

                for (auto it = saved_volatile_regs.rbegin();
                     it != saved_volatile_regs.rend(); ++it) {
                    if (it->via_stack) {
                        Instruction restore_volatile;
                        restore_volatile.opcode = OpCode::Pop;
                        restore_volatile.dst = it->reg;
                        lowered.push_back(std::move(restore_volatile));
                    } else {
                        EmitMoveToRegister(it->reg, Operand::Register(it->backup),
                                           lowered);
                    }
                }

                if (is_indirect) {
                    Instruction restore_scratch;
                    restore_scratch.opcode = OpCode::Pop;
                    restore_scratch.dst = kCallTargetScratch;
                    lowered.push_back(std::move(restore_scratch));
                }
                continue;
            }

            if (inst.opcode == OpCode::Return) {
                if (inst.a.kind != OperandKind::Null &&
                    !OperandIsRegister(inst.a, 0)) {
                    Instruction move_ret;
                    move_ret.opcode = OpCode::Move;
                    move_ret.dst = 0;
                    move_ret.a = inst.a;
                    lowered.push_back(std::move(move_ret));
                }
                Instruction jump_to_epilogue;
                jump_to_epilogue.opcode = OpCode::Jump;
                jump_to_epilogue.target = 0;
                lowered.push_back(std::move(jump_to_epilogue));
                return_jump_patches.push_back(lowered.size() - 1);
                continue;
            }

            if ((inst.opcode == OpCode::Binary || inst.opcode == OpCode::Compare) &&
                inst.a.kind == OperandKind::StackSlot &&
                inst.b.kind == OperandKind::StackSlot) {
                RegisterId scratch = kSelfRegister - 1;
                bool restore_scratch = true;
                if (inst.opcode == OpCode::Binary &&
                    inst.dst != kInvalidRegister &&
                    inst.dst != kStackPointerRegister &&
                    inst.dst != kBasePointerRegister) {
                    scratch = inst.dst;
                    restore_scratch = false;
                } else if (scratch == kStackPointerRegister ||
                           scratch == kBasePointerRegister) {
                    scratch = kSelfRegister - 2;
                }

                if (restore_scratch) {
                    Instruction save_scratch;
                    save_scratch.opcode = OpCode::Push;
                    save_scratch.a = Operand::Register(scratch);
                    lowered.push_back(std::move(save_scratch));
                }

                Instruction load_mem_lhs;
                load_mem_lhs.opcode = OpCode::Move;
                load_mem_lhs.dst = scratch;
                load_mem_lhs.a = inst.a;
                lowered.push_back(std::move(load_mem_lhs));

                Instruction rewritten = inst;
                rewritten.a = Operand::Register(scratch);
                lowered.push_back(std::move(rewritten));

                if (restore_scratch) {
                    Instruction restore_scratch_inst;
                    restore_scratch_inst.opcode = OpCode::Pop;
                    restore_scratch_inst.dst = scratch;
                    lowered.push_back(std::move(restore_scratch_inst));
                }
                continue;
            }

            lowered.push_back(inst);
            if (IsJumpOpcode(inst.opcode)) {
                jump_patches.emplace_back(lowered.size() - 1, inst.target);
            }
        }

        for (const auto &[new_index, old_target] : jump_patches) {
            if (old_target >= old_to_new_start.size()) {
                throw BytecodeException("calling convention lowering produced "
                                        "invalid jump target @" +
                                        std::to_string(old_target));
            }
            lowered[new_index].target = old_to_new_start[old_target];
        }

        const Address epilogue_pc = static_cast<Address>(lowered.size());

        Instruction restore_sp;
        restore_sp.opcode = OpCode::Move;
        restore_sp.dst = kStackPointerRegister;
        restore_sp.a = Operand::Register(kBasePointerRegister);
        lowered.push_back(std::move(restore_sp));

        for (auto it = saved_callee_regs.rbegin(); it != saved_callee_regs.rend();
             ++it) {
            Instruction restore_reg;
            restore_reg.opcode = OpCode::Pop;
            restore_reg.dst = *it;
            lowered.push_back(std::move(restore_reg));
        }

        Instruction pop_bp;
        pop_bp.opcode = OpCode::Pop;
        pop_bp.dst = kBasePointerRegister;
        lowered.push_back(std::move(pop_bp));

        Instruction ret;
        ret.opcode = OpCode::Return;
        ret.a = Operand::Null();
        lowered.push_back(std::move(ret));

        for (const std::size_t patch_index : return_jump_patches) {
            if (patch_index >= lowered.size()) {
                throw BytecodeException("calling convention lowering produced "
                                        "invalid return jump patch index");
            }
            lowered[patch_index].target = epilogue_pc;
        }

        if (old_entry >= old_to_new_start.size()) {
            throw BytecodeException("calling convention lowering produced "
                                    "invalid entry @" +
                                    std::to_string(old_entry));
        }
        lowered[prologue_jump_index].target = old_to_new_start[old_entry];
        function.entry_pc = 0;
        function.code = std::move(lowered);
        function.register_count = kRegisterCount;
        return true;
    }
};

} // namespace

std::unique_ptr<Pass> CreateCallingConventionPass() {
    return std::make_unique<CallingConventionPass>();
}

} // namespace compiler::bytecode
