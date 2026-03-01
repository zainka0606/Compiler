#include "Bytecode.h"
#include "Common/Identifier.h"
#include "passes/PassFactories.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace compiler::bytecode {

Operand Operand::Register(const RegisterId value) {
    Operand out;
    out.kind = OperandKind::Register;
    out.reg = value;
    return out;
}

Operand Operand::StackSlot(const SlotId value) {
    Operand out;
    out.kind = OperandKind::StackSlot;
    out.stack_slot = value;
    return out;
}

Operand Operand::Number(const double value) {
    Operand out;
    out.kind = OperandKind::Number;
    out.number = value;
    return out;
}

Operand Operand::String(std::string value) {
    Operand out;
    out.kind = OperandKind::String;
    out.text = std::move(value);
    return out;
}

Operand Operand::Bool(const bool value) {
    Operand out;
    out.kind = OperandKind::Bool;
    out.boolean = value;
    return out;
}

Operand Operand::Char(const char value) {
    Operand out;
    out.kind = OperandKind::Char;
    out.character = value;
    return out;
}

Operand Operand::Null() {
    Operand out;
    out.kind = OperandKind::Null;
    return out;
}

namespace {

constexpr std::array kBundleMagic = {'N', 'E', 'O', 'N',
                                              'B', 'C', '0', '4'};

constexpr std::array<std::string_view, 30> kBuiltinFunctionNames = {
    "sin",
    "cos",
    "tan",
    "sqrt",
    "abs",
    "exp",
    "ln",
    "log10",
    "pow",
    "min",
    "max",
    "sum",
    "print",
    "println",
    "readln",
    "input",
    "read_file",
    "write_file",
    "append_file",
    "read_binary_file",
    "write_binary_file",
    "append_binary_file",
    "file_exists",
    "file_size",
    "len",
    "__array_push",
    "__array_pop",
    "__string_concat",
    "__cast",
    "alloc",
};

void WriteBytes(std::ostream &out, const void *data, const std::size_t size) {
    out.write(static_cast<const char *>(data),
              static_cast<std::streamsize>(size));
    if (!out.good()) {
        throw BytecodeException("failed while writing bytecode bundle");
    }
}

void ReadBytes(std::istream &in, void *data, const std::size_t size) {
    in.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
    if (!in.good()) {
        throw BytecodeException("unexpected end of bytecode bundle");
    }
}

void WriteU8(std::ostream &out, const std::uint8_t value) {
    WriteBytes(out, &value, sizeof(value));
}

void WriteU32(std::ostream &out, const std::uint32_t value) {
    WriteBytes(out, &value, sizeof(value));
}

void WriteU64(std::ostream &out, const std::uint64_t value) {
    WriteBytes(out, &value, sizeof(value));
}

std::uint8_t ReadU8(std::istream &in) {
    std::uint8_t value = 0;
    ReadBytes(in, &value, sizeof(value));
    return value;
}

std::uint32_t ReadU32(std::istream &in) {
    std::uint32_t value = 0;
    ReadBytes(in, &value, sizeof(value));
    return value;
}

std::uint64_t ReadU64(std::istream &in) {
    std::uint64_t value = 0;
    ReadBytes(in, &value, sizeof(value));
    return value;
}

void WriteString(std::ostream &out, const std::string &text) {
    WriteU64(out, text.size());
    if (!text.empty()) {
        WriteBytes(out, text.data(), text.size());
    }
}

std::string ReadString(std::istream &in) {
    const std::uint64_t size = ReadU64(in);
    if (size >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("string length exceeds platform limits");
    }
    std::string out(size, '\0');
    if (!out.empty()) {
        ReadBytes(in, out.data(), out.size());
    }
    return out;
}

void WriteStringVector(std::ostream &out,
                       const std::vector<std::string> &values) {
    WriteU64(out, values.size());
    for (const std::string &value : values) {
        WriteString(out, value);
    }
}

std::vector<std::string> ReadStringVector(std::istream &in) {
    const std::uint64_t count = ReadU64(in);
    if (count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("vector length exceeds platform limits");
    }

    std::vector<std::string> out;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        out.push_back(ReadString(in));
    }
    return out;
}

template <typename T>
void WriteVectorScalar(std::ostream &out, const std::vector<T> &values) {
    static_assert(std::is_integral_v<T>,
                  "WriteVectorScalar only supports integral element types");
    WriteU64(out, static_cast<std::uint64_t>(values.size()));
    for (const T value : values) {
        if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
            WriteU32(out, static_cast<std::uint32_t>(value));
        } else if constexpr (sizeof(T) == sizeof(std::uint64_t)) {
            WriteU64(out, static_cast<std::uint64_t>(value));
        } else {
            static_assert(sizeof(T) == 0,
                          "unsupported scalar width in WriteVectorScalar");
        }
    }
}

template <typename T> std::vector<T> ReadVectorScalar(std::istream &in) {
    static_assert(std::is_integral_v<T>,
                  "ReadVectorScalar only supports integral element types");
    const std::uint64_t count = ReadU64(in);
    if (count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("vector length exceeds platform limits");
    }

    std::vector<T> out;
    out.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
            out.push_back(static_cast<T>(ReadU32(in)));
        } else if constexpr (sizeof(T) == sizeof(std::uint64_t)) {
            out.push_back(static_cast<T>(ReadU64(in)));
        } else {
            static_assert(sizeof(T) == 0,
                          "unsupported scalar width in ReadVectorScalar");
        }
    }
    return out;
}

std::vector<std::string>
SortedStringKeys(const std::unordered_map<std::string, std::uint32_t> &values) {
    std::vector<std::string> keys;
    keys.reserve(values.size());
    for (const auto &[key, _] : values) {
        keys.push_back(key);
    }
    std::ranges::sort(keys);
    return keys;
}

std::vector<std::string>
SortedStringKeys(const std::unordered_map<std::string, std::string> &values) {
    std::vector<std::string> keys;
    keys.reserve(values.size());
    for (const auto &[key, _] : values) {
        keys.push_back(key);
    }
    std::ranges::sort(keys);
    return keys;
}

void WriteStringU32Map(
    std::ostream &out,
    const std::unordered_map<std::string, std::uint32_t> &map) {
    const std::vector<std::string> keys = SortedStringKeys(map);
    WriteU64(out, keys.size());
    for (const std::string &key : keys) {
        WriteString(out, key);
        WriteU32(out, map.at(key));
    }
}

std::unordered_map<std::string, std::uint32_t>
ReadStringU32Map(std::istream &in) {
    const std::uint64_t count = ReadU64(in);
    if (count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("map length exceeds platform limits");
    }

    std::unordered_map<std::string, std::uint32_t> out;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::string key = ReadString(in);
        const std::uint32_t value = ReadU32(in);
        if (!out.emplace(key, value).second) {
            throw BytecodeException("duplicate key while decoding map: " + key);
        }
    }
    return out;
}

void WriteStringStringMap(
    std::ostream &out,
    const std::unordered_map<std::string, std::string> &map) {
    const std::vector<std::string> keys = SortedStringKeys(map);
    WriteU64(out, keys.size());
    for (const std::string &key : keys) {
        WriteString(out, key);
        WriteString(out, map.at(key));
    }
}

std::unordered_map<std::string, std::string>
ReadStringStringMap(std::istream &in) {
    const std::uint64_t count = ReadU64(in);
    if (count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("map length exceeds platform limits");
    }

    std::unordered_map<std::string, std::string> out;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::string key = ReadString(in);
        const std::string value = ReadString(in);
        if (!out.emplace(key, value).second) {
            throw BytecodeException("duplicate key while decoding map: " + key);
        }
    }
    return out;
}

void WriteOperand(
    std::ostream &out, const Operand &operand,
    const std::unordered_map<std::string, std::uint32_t> &rodata_index_by_text) {
    WriteU8(out, static_cast<std::uint8_t>(operand.kind));
    switch (operand.kind) {
    case OperandKind::Register:
        WriteU32(out, operand.reg);
        break;
    case OperandKind::StackSlot:
        WriteU32(out, operand.stack_slot);
        break;
    case OperandKind::Number:
        WriteU64(out, std::bit_cast<std::uint64_t>(operand.number));
        break;
    case OperandKind::String: {
        const auto found = rodata_index_by_text.find(operand.text);
        if (found == rodata_index_by_text.end()) {
            throw BytecodeException("string constant missing from rodata table");
        }
        WriteU32(out, found->second);
        break;
    }
    case OperandKind::Bool:
        WriteU8(out, operand.boolean ? 1U : 0U);
        break;
    case OperandKind::Char:
        WriteU8(out, static_cast<std::uint8_t>(operand.character));
        break;
    case OperandKind::Null:
        break;
    }
}

Operand ReadOperand(std::istream &in, const std::vector<std::string> &rodata) {
    const auto kind = static_cast<OperandKind>(ReadU8(in));
    switch (kind) {
    case OperandKind::Register:
        return Operand::Register(ReadU32(in));
    case OperandKind::StackSlot:
        return Operand::StackSlot(ReadU32(in));
    case OperandKind::Number:
        return Operand::Number(std::bit_cast<double>(ReadU64(in)));
    case OperandKind::String: {
        const std::uint32_t index = ReadU32(in);
        if (index >= rodata.size()) {
            throw BytecodeException("rodata index out of range in operand");
        }
        return Operand::String(rodata[index]);
    }
    case OperandKind::Bool:
        return Operand::Bool(ReadU8(in) != 0);
    case OperandKind::Char:
        return Operand::Char(static_cast<char>(ReadU8(in)));
    case OperandKind::Null:
        return Operand::Null();
    }
    throw BytecodeException("invalid operand kind while decoding bytecode");
}

void WriteOperandVector(
    std::ostream &out, const std::vector<Operand> &values,
    const std::unordered_map<std::string, std::uint32_t> &rodata_index_by_text) {
    WriteU64(out, values.size());
    for (const Operand &value : values) {
        WriteOperand(out, value, rodata_index_by_text);
    }
}

std::vector<Operand> ReadOperandVector(std::istream &in,
                                       const std::vector<std::string> &rodata) {
    const std::uint64_t count = ReadU64(in);
    if (count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("vector length exceeds platform limits");
    }

    std::vector<Operand> out;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        out.push_back(ReadOperand(in, rodata));
    }
    return out;
}

Operand LowerValueRef(const ir::ValueRef &value) {
    switch (value.kind) {
    case ir::ValueKind::Temp:
        return Operand::Register(value.temp);
    case ir::ValueKind::Number:
        return Operand::Number(value.number);
    case ir::ValueKind::String:
        return Operand::String(value.text);
    case ir::ValueKind::Bool:
        return Operand::Bool(value.boolean);
    case ir::ValueKind::Char:
        return Operand::Char(value.character);
    case ir::ValueKind::Null:
        return Operand::Null();
    }
    return Operand::Null();
}

bool IsImmediateOperand(const Operand &operand) {
    return operand.kind != OperandKind::Register &&
           operand.kind != OperandKind::StackSlot;
}

bool IsComparisonBinaryOp(const ir::BinaryOp op) {
    return op == ir::BinaryOp::Equal || op == ir::BinaryOp::NotEqual ||
           op == ir::BinaryOp::Less || op == ir::BinaryOp::LessEqual ||
           op == ir::BinaryOp::Greater || op == ir::BinaryOp::GreaterEqual;
}

struct PendingJumpPatch {
    std::size_t instruction_index = 0;
    ir::BlockId target_block = 0;
};

void QueueJump(std::vector<Instruction> &code,
               std::vector<PendingJumpPatch> &patches,
               const OpCode opcode, const ir::BlockId target_block,
               const Operand &condition = Operand::Null()) {
    Instruction jump;
    jump.opcode = opcode;
    jump.target = target_block;
    jump.a = condition;
    code.push_back(std::move(jump));
    patches.push_back(
        PendingJumpPatch{.instruction_index = code.size() - 1,
                         .target_block = target_block});
}

void PatchQueuedJumps(std::vector<Instruction> &code,
                      const std::vector<PendingJumpPatch> &patches,
                      const std::vector<Address> &block_offsets,
                      const std::string_view function_name) {
    for (const PendingJumpPatch &patch : patches) {
        if (patch.target_block >= block_offsets.size()) {
            throw BytecodeException("invalid block target b" +
                                    std::to_string(patch.target_block) +
                                    " in function '" +
                                    std::string(function_name) + "'");
        }
        if (patch.instruction_index >= code.size()) {
            throw BytecodeException("invalid jump patch index in function '" +
                                    std::string(function_name) + "'");
        }
        code[patch.instruction_index].target = block_offsets[patch.target_block];
    }
}

std::optional<OpCode> JumpAliasForComparison(const ir::BinaryOp op) {
    switch (op) {
    case ir::BinaryOp::Equal:
        return OpCode::JumpZero;
    case ir::BinaryOp::NotEqual:
        return OpCode::JumpNotZero;
    case ir::BinaryOp::Less:
        return OpCode::JumpLess;
    case ir::BinaryOp::LessEqual:
        return OpCode::JumpLessEqual;
    case ir::BinaryOp::Greater:
        return OpCode::JumpGreater;
    case ir::BinaryOp::GreaterEqual:
        return OpCode::JumpGreaterEqual;
    default:
        return std::nullopt;
    }
}

void BumpTempUseCount(const ir::ValueRef &value,
                      std::unordered_map<ir::LocalId, std::size_t> &counts) {
    if (value.kind == ir::ValueKind::Temp) {
        ++counts[value.temp];
    }
}

void AppendLoweredInstruction(const ir::Instruction &instruction,
                              std::vector<Instruction> &out_code,
                              ir::LocalId &next_temp,
                              std::vector<GlobalVariable> *globals) {
    std::visit(
        [&](const auto &inst) {
            using T = std::decay_t<decltype(inst)>;
            Instruction out;
            if constexpr (std::is_same_v<T, ir::LoadLocalInst>) {
                out.opcode = OpCode::Load;
                out.load_mode = LoadMode::StackRelative;
                out.dst = inst.dst;
                out.slot = inst.slot;
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T,
                                                ir::StoreLocalInst>) {
                out.opcode = OpCode::Store;
                out.store_mode = StoreMode::StackRelative;
                out.slot = inst.slot;
                out.a = LowerValueRef(inst.value);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<
                                     T, ir::DeclareGlobalInst>) {
                const Operand lowered_value = LowerValueRef(inst.value);
                if (globals != nullptr && IsImmediateOperand(lowered_value)) {
                    globals->push_back(GlobalVariable{
                        .name = inst.name,
                        .type_name = inst.type_name,
                        .value = lowered_value,
                    });
                    return;
                }
                out.opcode = OpCode::DeclareGlobal;
                out.text = inst.name;
                out.text2 = inst.type_name;
                out.a = lowered_value;
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T,
                                                ir::LoadGlobalInst>) {
                out.opcode = OpCode::Load;
                out.load_mode = LoadMode::Global;
                out.dst = inst.dst;
                out.text = inst.name;
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<
                                     T, ir::StoreGlobalInst>) {
                out.opcode = OpCode::Store;
                out.store_mode = StoreMode::Global;
                out.text = inst.name;
                out.a = LowerValueRef(inst.value);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T, ir::MoveInst>) {
                out.opcode = OpCode::Move;
                out.dst = inst.dst;
                out.a = LowerValueRef(inst.src);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T, ir::UnaryInst>) {
                out.opcode = OpCode::Unary;
                out.dst = inst.dst;
                out.unary_op = inst.op;
                out.a = LowerValueRef(inst.value);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T, ir::BinaryInst>) {
                out.opcode = OpCode::Binary;
                out.dst = inst.dst;
                out.binary_op = inst.op;
                out.a = LowerValueRef(inst.lhs);
                out.b = LowerValueRef(inst.rhs);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T,
                                                ir::MakeArrayInst>) {
                out.opcode = OpCode::MakeArray;
                out.dst = inst.dst;
                out.operands.reserve(inst.elements.size());
                for (const ir::ValueRef &value : inst.elements) {
                    out.operands.push_back(LowerValueRef(value));
                }
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T,
                                                ir::ArrayLoadInst>) {
                out.opcode = OpCode::Load;
                out.load_mode = LoadMode::ArrayElement;
                out.dst = inst.dst;
                out.a = LowerValueRef(inst.array);
                out.b = LowerValueRef(inst.index);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<T,
                                                ir::ArrayStoreInst>) {
                out.opcode = OpCode::Store;
                out.store_mode = StoreMode::ArrayElement;
                out.a = LowerValueRef(inst.value);
                out.b = LowerValueRef(inst.array);
                out.c = LowerValueRef(inst.index);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<
                                     T, ir::ResolveFieldOffsetInst>) {
                throw BytecodeException(
                    "unresolved field offset for member '" + inst.member +
                    "' while lowering function; field offsets must be "
                    "resolved at compile time");
            } else if constexpr (std::is_same_v<T,
                                                ir::ObjectLoadInst>) {
                out.opcode = OpCode::Load;
                out.load_mode = LoadMode::ObjectOffset;
                out.dst = inst.dst;
                out.a = LowerValueRef(inst.object);
                out.b = LowerValueRef(inst.offset);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<
                                     T, ir::ObjectStoreInst>) {
                out.opcode = OpCode::Store;
                out.store_mode = StoreMode::ObjectOffset;
                out.a = LowerValueRef(inst.value);
                out.b = LowerValueRef(inst.object);
                out.c = LowerValueRef(inst.offset);
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<
                                     T, ir::ResolveMethodSlotInst>) {
                throw BytecodeException(
                    "unresolved method slot for method '" + inst.method +
                    "' while lowering function; virtual slots must be "
                    "resolved at compile time");
            } else if constexpr (std::is_same_v<T, ir::CallInst>) {
                if (inst.callee == "stack_alloc") {
                    if (inst.args.size() != 1 ||
                        inst.args[0].kind != ir::ValueKind::String ||
                        inst.args[0].text.empty()) {
                        throw BytecodeException(
                            "stack_alloc lowering expects exactly one non-empty "
                            "string class-name argument");
                    }
                    out.opcode = OpCode::StackAllocObject;
                    out.dst = inst.dst;
                    out.text = inst.args[0].text;
                    out_code.push_back(std::move(out));
                    return;
                }
                out.opcode = OpCode::Call;
                out.dst = inst.dst;
                out.text = inst.callee;
                out.operands.reserve(inst.args.size());
                for (const ir::ValueRef &arg : inst.args) {
                    out.operands.push_back(LowerValueRef(arg));
                }
                out_code.push_back(std::move(out));
            } else if constexpr (std::is_same_v<
                                     T, ir::VirtualCallInst>) {
                const ir::LocalId callee_reg = next_temp++;

                Instruction load_vtable;
                load_vtable.opcode = OpCode::Load;
                load_vtable.load_mode = LoadMode::ObjectOffset;
                load_vtable.dst = callee_reg;
                load_vtable.a = LowerValueRef(inst.object);
                load_vtable.b = Operand::Number(0.0);
                out_code.push_back(std::move(load_vtable));

                Instruction load_method;
                load_method.opcode = OpCode::Load;
                load_method.load_mode = LoadMode::ArrayElement;
                load_method.dst = callee_reg;
                load_method.a = Operand::Register(callee_reg);
                load_method.b = LowerValueRef(inst.slot);
                out_code.push_back(std::move(load_method));

                Instruction call_reg;
                call_reg.opcode = OpCode::CallRegister;
                call_reg.dst = inst.dst;
                call_reg.a = Operand::Register(callee_reg);
                call_reg.operands.reserve(inst.args.size() + 1);
                call_reg.operands.push_back(LowerValueRef(inst.object));
                for (const ir::ValueRef &arg : inst.args) {
                    call_reg.operands.push_back(LowerValueRef(arg));
                }
                out_code.push_back(std::move(call_reg));
            }
        },
        instruction);
}

void EmitTerminator(const ir::Terminator &terminator,
                    std::vector<Instruction> &out_code,
                    std::vector<PendingJumpPatch> &patches,
                    const std::optional<ir::BinaryInst> &compare_alias) {
    if (const auto *jump = std::get_if<ir::JumpTerm>(&terminator)) {
        QueueJump(out_code, patches, OpCode::Jump, jump->target);
        return;
    }

    if (const auto *branch =
            std::get_if<ir::BranchTerm>(&terminator)) {
        if (compare_alias.has_value() &&
            IsComparisonBinaryOp(compare_alias->op)) {
            Instruction cmp;
            cmp.opcode = OpCode::Compare;
            cmp.a = LowerValueRef(compare_alias->lhs);
            cmp.b = LowerValueRef(compare_alias->rhs);
            out_code.push_back(std::move(cmp));

            const std::optional<OpCode> direct_alias =
                JumpAliasForComparison(compare_alias->op);
            if (direct_alias.has_value()) {
                QueueJump(out_code, patches, *direct_alias, branch->true_target);
                QueueJump(out_code, patches, OpCode::Jump, branch->false_target);
                return;
            }
        }

        QueueJump(out_code, patches, OpCode::JumpIfFalse, branch->false_target,
                  LowerValueRef(branch->condition));
        QueueJump(out_code, patches, OpCode::Jump, branch->true_target);
        return;
    }

    if (const auto *ret = std::get_if<ir::ReturnTerm>(&terminator)) {
        if (ret->value.has_value()) {
            Instruction move_ret;
            move_ret.opcode = OpCode::Move;
            move_ret.dst = 0;
            move_ret.a = LowerValueRef(*ret->value);
            out_code.push_back(std::move(move_ret));
        } else {
            Instruction move_ret_null;
            move_ret_null.opcode = OpCode::Move;
            move_ret_null.dst = 0;
            move_ret_null.a = Operand::Null();
            out_code.push_back(std::move(move_ret_null));
        }
        Instruction bc_ret;
        bc_ret.opcode = OpCode::Return;
        bc_ret.a = Operand::Null();
        out_code.push_back(std::move(bc_ret));
        return;
    }

    throw BytecodeException("unsupported terminator while lowering IR");
}

Function LowerFunction(const ir::Function &function,
                       std::vector<GlobalVariable> *globals) {
    std::unordered_map<ir::LocalId, std::size_t> temp_use_counts;
    for (const ir::BasicBlock &block : function.blocks) {
        for (const ir::Instruction &instruction : block.instructions) {
            std::visit(
                [&](const auto &inst) {
                    using T = std::decay_t<decltype(inst)>;
                    if constexpr (std::is_same_v<T, ir::StoreLocalInst>) {
                        BumpTempUseCount(inst.value, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::DeclareGlobalInst>) {
                        BumpTempUseCount(inst.value, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::StoreGlobalInst>) {
                        BumpTempUseCount(inst.value, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::MoveInst>) {
                        BumpTempUseCount(inst.src, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::UnaryInst>) {
                        BumpTempUseCount(inst.value, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::BinaryInst>) {
                        BumpTempUseCount(inst.lhs, temp_use_counts);
                        BumpTempUseCount(inst.rhs, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::MakeArrayInst>) {
                        for (const ir::ValueRef &value : inst.elements) {
                            BumpTempUseCount(value, temp_use_counts);
                        }
                    } else if constexpr (std::is_same_v<T, ir::ArrayLoadInst>) {
                        BumpTempUseCount(inst.array, temp_use_counts);
                        BumpTempUseCount(inst.index, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::ArrayStoreInst>) {
                        BumpTempUseCount(inst.array, temp_use_counts);
                        BumpTempUseCount(inst.index, temp_use_counts);
                        BumpTempUseCount(inst.value, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::ResolveFieldOffsetInst>) {
                        BumpTempUseCount(inst.object, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::ObjectLoadInst>) {
                        BumpTempUseCount(inst.object, temp_use_counts);
                        BumpTempUseCount(inst.offset, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::ObjectStoreInst>) {
                        BumpTempUseCount(inst.object, temp_use_counts);
                        BumpTempUseCount(inst.offset, temp_use_counts);
                        BumpTempUseCount(inst.value, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::ResolveMethodSlotInst>) {
                        BumpTempUseCount(inst.object, temp_use_counts);
                    } else if constexpr (std::is_same_v<T, ir::CallInst>) {
                        for (const ir::ValueRef &arg : inst.args) {
                            BumpTempUseCount(arg, temp_use_counts);
                        }
                    } else if constexpr (std::is_same_v<T, ir::VirtualCallInst>) {
                        BumpTempUseCount(inst.object, temp_use_counts);
                        BumpTempUseCount(inst.slot, temp_use_counts);
                        for (const ir::ValueRef &arg : inst.args) {
                            BumpTempUseCount(arg, temp_use_counts);
                        }
                    }
                },
                instruction);
        }

        if (!block.terminator.has_value()) {
            throw BytecodeException("block b" + std::to_string(block.id) +
                                    " in function '" + function.name +
                                    "' has no terminator");
        }
        std::visit(
            [&](const auto &term) {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, ir::BranchTerm>) {
                    BumpTempUseCount(term.condition, temp_use_counts);
                } else if constexpr (std::is_same_v<T, ir::ReturnTerm>) {
                    if (term.value.has_value()) {
                        BumpTempUseCount(*term.value, temp_use_counts);
                    }
                }
            },
            *block.terminator);
    }

    Function out;
    out.name = function.name;
    out.params = function.params;
    out.param_types = function.param_types;
    out.param_slots = function.param_slots;
    out.slot_names = function.slot_names;
    out.slot_types = function.slot_types;
    out.local_count = function.next_slot;
    out.stack_slot_count = function.next_slot;
    ir::LocalId lowered_next_temp = function.next_temp;
    out.register_count = lowered_next_temp;
    out.is_method = function.is_method;
    out.owning_class = function.owning_class;
    out.method_name = function.method_name;
    out.code.reserve(function.blocks.size() * 4 + 16);
    std::vector<Address> block_offsets(function.blocks.size(), 0);
    std::vector<PendingJumpPatch> pending_jumps;

    for (const ir::BasicBlock &block : function.blocks) {
        if (block.id >= block_offsets.size()) {
            throw BytecodeException("invalid block id while lowering function '" +
                                    function.name + "'");
        }
        block_offsets[block.id] = static_cast<Address>(out.code.size());

        std::optional<ir::BinaryInst> compare_alias;
        std::size_t instruction_limit = block.instructions.size();
        if (const auto *branch =
                std::get_if<ir::BranchTerm>(&*block.terminator);
            branch != nullptr && branch->condition.kind == ir::ValueKind::Temp &&
            !block.instructions.empty()) {
            if (const auto *binary =
                    std::get_if<ir::BinaryInst>(&block.instructions.back());
                binary != nullptr && binary->dst == branch->condition.temp &&
                IsComparisonBinaryOp(binary->op)) {
                const auto use_it = temp_use_counts.find(binary->dst);
                const std::size_t use_count =
                    use_it == temp_use_counts.end() ? 0 : use_it->second;
                if (use_count <= 1) {
                    compare_alias = *binary;
                    --instruction_limit;
                }
            }
        }

        for (std::size_t i = 0; i < instruction_limit; ++i) {
            AppendLoweredInstruction(block.instructions[i], out.code,
                                     lowered_next_temp, globals);
        }
        EmitTerminator(*block.terminator, out.code, pending_jumps, compare_alias);
    }

    if (function.entry >= block_offsets.size()) {
        throw BytecodeException("invalid entry block in function '" +
                                function.name + "'");
    }
    out.entry_pc = block_offsets[function.entry];
    out.register_count = lowered_next_temp;
    PatchQueuedJumps(out.code, pending_jumps, block_offsets, function.name);

    return out;
}

ClassInfo LowerClassInfo(const ir::ClassInfo &class_info) {
    return ClassInfo{
        .name = class_info.name,
        .base_class = class_info.base_class,
        .fields = class_info.fields,
        .field_types = class_info.field_types,
        .field_offsets = class_info.field_offsets,
        .method_slots = class_info.method_slots,
        .vtable_functions = class_info.vtable_functions,
        .method_functions = class_info.method_functions,
        .constructor_function = class_info.constructor_function,
    };
}

std::string NumberToString(const double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

std::string EscapeString(const std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\"':
            out += "\\\"";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

void CollectOperandRodata(
    std::vector<std::string> &rodata,
    std::unordered_map<std::string, std::uint32_t> &index_by_text,
    const Operand &operand) {
    if (operand.kind != OperandKind::String) {
        return;
    }
    if (index_by_text.contains(operand.text)) {
        return;
    }
    index_by_text.emplace(operand.text, static_cast<std::uint32_t>(rodata.size()));
    rodata.push_back(operand.text);
}

void CollectProgramRodata(
    const Program &program, std::vector<std::string> &rodata,
    std::unordered_map<std::string, std::uint32_t> &index_by_text) {
    for (const GlobalVariable &global : program.globals) {
        CollectOperandRodata(rodata, index_by_text, global.value);
    }
    for (const Function &function : program.functions) {
        for (const Instruction &inst : function.code) {
            CollectOperandRodata(rodata, index_by_text, inst.a);
            CollectOperandRodata(rodata, index_by_text, inst.b);
            CollectOperandRodata(rodata, index_by_text, inst.c);
            for (const Operand &operand : inst.operands) {
                CollectOperandRodata(rodata, index_by_text, operand);
            }
        }
    }
}

std::vector<std::string> BuildProgramRodata(const Program &program) {
    std::vector<std::string> rodata;
    std::unordered_map<std::string, std::uint32_t> index_by_text;
    CollectProgramRodata(program, rodata, index_by_text);
    return rodata;
}

std::vector<std::string> BuildBundleRodata(const ProgramBundle &bundle) {
    std::vector<std::string> rodata;
    std::unordered_map<std::string, std::uint32_t> index_by_text;
    for (const ProgramUnit &unit : bundle.prelude_units) {
        CollectProgramRodata(unit.program, rodata, index_by_text);
    }
    CollectProgramRodata(bundle.program, rodata, index_by_text);
    return rodata;
}

using RodataLabelMap = std::unordered_map<std::string, std::string>;
using AddressLabelMap = std::unordered_map<Address, std::string>;

std::string BuildIndexedLabel(const std::string_view prefix,
                              const std::size_t index) {
    std::ostringstream out;
    out << prefix << "_" << std::setfill('0') << std::setw(4) << index;
    return out.str();
}

RodataLabelMap BuildRodataLabelMap(const std::vector<std::string> &rodata) {
    RodataLabelMap labels;
    labels.reserve(rodata.size());
    for (std::size_t i = 0; i < rodata.size(); ++i) {
        labels.emplace(rodata[i], BuildIndexedLabel("ro_data", i));
    }
    return labels;
}

std::string FunctionLabelForAssembly(const std::string_view function_name,
                                     const std::size_t unit_index,
                                     const std::size_t function_index) {
    return "fn_u" + std::to_string(unit_index) + "_f" +
           std::to_string(function_index) + "_" +
           common::SanitizeIdentifier(function_name, "function");
}

std::string JumpLabelForAssembly(const std::string_view function_label,
                                 const Address target_pc) {
    return "lb_" + std::string(function_label) + "_" +
           BuildIndexedLabel("pc", target_pc);
}

std::string ResolveTargetLabel(const Address target,
                               const AddressLabelMap *labels) {
    if (labels != nullptr) {
        const auto found = labels->find(target);
        if (found != labels->end()) {
            return found->second;
        }
    }
    return "@" + std::to_string(target);
}

std::string RegisterNameForAssembly(const RegisterId reg) {
    if (reg == kStackPointerRegister) {
        return "sp";
    }
    if (reg == kBasePointerRegister) {
        return "bp";
    }
    return "r" + std::to_string(reg);
}

std::string OperandToAssembly(const Operand &operand,
                              const RodataLabelMap *rodata_labels = nullptr) {
    switch (operand.kind) {
    case OperandKind::Register:
        return RegisterNameForAssembly(operand.reg);
    case OperandKind::StackSlot:
        return "[bp+s" + std::to_string(operand.stack_slot) + "]";
    case OperandKind::Number:
        return NumberToString(operand.number);
    case OperandKind::String: {
        if (rodata_labels != nullptr) {
            const auto found = rodata_labels->find(operand.text);
            if (found != rodata_labels->end()) {
                return found->second;
            }
        }
        return "\"" + EscapeString(operand.text) + "\"";
    }
    case OperandKind::Bool:
        return operand.boolean ? "true" : "false";
    case OperandKind::Char:
        return std::string("'") + operand.character + "'";
    case OperandKind::Null:
        return "null";
    }
    return "null";
}

std::string ResultTargetToAssembly(const Instruction &instruction) {
    if (instruction.dst != kInvalidRegister) {
        return RegisterNameForAssembly(instruction.dst);
    }
    if (instruction.dst_slot != kInvalidSlot) {
        return "[bp+s" + std::to_string(instruction.dst_slot) + "]";
    }
    return "<discard>";
}

const char *UnaryOpName(const ir::UnaryOp op) {
    switch (op) {
    case ir::UnaryOp::Negate:
        return "neg";
    case ir::UnaryOp::LogicalNot:
        return "lnot";
    case ir::UnaryOp::BitwiseNot:
        return "bnot";
    }
    return "?";
}

const char *BinaryOpName(const ir::BinaryOp op) {
    switch (op) {
    case ir::BinaryOp::Add:
        return "add";
    case ir::BinaryOp::Subtract:
        return "sub";
    case ir::BinaryOp::Multiply:
        return "mul";
    case ir::BinaryOp::Divide:
        return "div";
    case ir::BinaryOp::IntDivide:
        return "idiv";
    case ir::BinaryOp::Modulo:
        return "mod";
    case ir::BinaryOp::Pow:
        return "pow";
    case ir::BinaryOp::BitwiseAnd:
        return "band";
    case ir::BinaryOp::BitwiseOr:
        return "bor";
    case ir::BinaryOp::BitwiseXor:
        return "bxor";
    case ir::BinaryOp::ShiftLeft:
        return "shl";
    case ir::BinaryOp::ShiftRight:
        return "shr";
    case ir::BinaryOp::Equal:
        return "eq";
    case ir::BinaryOp::NotEqual:
        return "neq";
    case ir::BinaryOp::Less:
        return "lt";
    case ir::BinaryOp::LessEqual:
        return "le";
    case ir::BinaryOp::Greater:
        return "gt";
    case ir::BinaryOp::GreaterEqual:
        return "ge";
    }
    return "?";
}

std::string InstructionToAssembly(const Instruction &instruction,
                                  const RodataLabelMap *rodata_labels = nullptr,
                                  const AddressLabelMap *jump_labels = nullptr,
                                  const AddressLabelMap *call_labels = nullptr) {
    std::ostringstream out;
    switch (instruction.opcode) {
    case OpCode::Nop:
        out << "nop";
        break;
    case OpCode::Load:
        out << "ld " << ResultTargetToAssembly(instruction) << ", ";
        switch (instruction.load_mode) {
        case LoadMode::StackRelative:
            out << "[bp+s" << instruction.slot << "]";
            break;
        case LoadMode::StackAbsolute:
            out << "[stk+" << instruction.slot << "]";
            break;
        case LoadMode::Global:
            out << "@" << instruction.text;
            break;
        case LoadMode::ArrayElement:
            out << OperandToAssembly(instruction.a, rodata_labels) << "["
                << OperandToAssembly(instruction.b, rodata_labels) << "]";
            break;
        case LoadMode::ObjectOffset:
            out << OperandToAssembly(instruction.a, rodata_labels) << "["
                << OperandToAssembly(instruction.b, rodata_labels) << "]";
            break;
        case LoadMode::FieldOffsetByName:
            out << "fieldoff " << OperandToAssembly(instruction.a, rodata_labels) << ", ."
                << instruction.text;
            break;
        case LoadMode::MethodSlotByName:
            out << "mslot " << OperandToAssembly(instruction.a, rodata_labels) << ", ."
                << instruction.text;
            break;
        case LoadMode::MethodFunctionBySlot:
            out << "mfn " << OperandToAssembly(instruction.a, rodata_labels) << ", "
                << OperandToAssembly(instruction.b, rodata_labels);
            break;
        }
        break;
    case OpCode::Store:
        out << "st ";
        switch (instruction.store_mode) {
        case StoreMode::StackRelative:
            out << "[bp+s" << instruction.slot << "], "
                << OperandToAssembly(instruction.a, rodata_labels);
            break;
        case StoreMode::StackAbsolute:
            out << "[stk+" << instruction.slot << "], "
                << OperandToAssembly(instruction.a, rodata_labels);
            break;
        case StoreMode::Global:
            out << "@" << instruction.text << ", "
                << OperandToAssembly(instruction.a, rodata_labels);
            break;
        case StoreMode::ArrayElement:
            out << OperandToAssembly(instruction.b, rodata_labels) << "["
                << OperandToAssembly(instruction.c, rodata_labels) << "], "
                << OperandToAssembly(instruction.a, rodata_labels);
            break;
        case StoreMode::ObjectOffset:
            out << OperandToAssembly(instruction.b, rodata_labels) << "["
                << OperandToAssembly(instruction.c, rodata_labels) << "], "
                << OperandToAssembly(instruction.a, rodata_labels);
            break;
        }
        break;
    case OpCode::Push:
        out << "push " << OperandToAssembly(instruction.a, rodata_labels);
        break;
    case OpCode::Pop:
        out << "pop " << ResultTargetToAssembly(instruction);
        break;
    case OpCode::DeclareGlobal:
        out << "defg @" << instruction.text;
        if (!instruction.text2.empty()) {
            out << ":" << instruction.text2;
        }
        out << ", " << OperandToAssembly(instruction.a, rodata_labels);
        break;
    case OpCode::Move:
        out << "mov " << ResultTargetToAssembly(instruction) << ", "
            << OperandToAssembly(instruction.a, rodata_labels);
        break;
    case OpCode::Unary:
        out << UnaryOpName(instruction.unary_op) << " "
            << ResultTargetToAssembly(instruction)
            << ", " << OperandToAssembly(instruction.a, rodata_labels);
        break;
    case OpCode::Binary:
        out << BinaryOpName(instruction.binary_op) << " "
            << ResultTargetToAssembly(instruction)
            << ", " << OperandToAssembly(instruction.a, rodata_labels) << ", "
            << OperandToAssembly(instruction.b, rodata_labels);
        break;
    case OpCode::Compare:
        out << "cmp " << OperandToAssembly(instruction.a, rodata_labels) << ", "
            << OperandToAssembly(instruction.b, rodata_labels);
        break;
    case OpCode::MakeArray:
        out << "array " << ResultTargetToAssembly(instruction) << ", [";
        for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << OperandToAssembly(instruction.operands[i], rodata_labels);
        }
        out << "]";
        break;
    case OpCode::StackAllocObject:
        out << "salloc " << ResultTargetToAssembly(instruction) << ", ."
            << instruction.text;
        break;
    case OpCode::Call:
        if (call_labels != nullptr) {
            out << "call " << ResolveTargetLabel(instruction.target, call_labels);
            break;
        }
        if (const std::string_view builtin_name =
                BuiltinNameForAddress(instruction.target);
            !builtin_name.empty()) {
            out << "call builtin_"
                << common::SanitizeIdentifier(builtin_name, "builtin");
            break;
        }
        out << "call @" << instruction.target;
        break;
    case OpCode::CallRegister:
        out << "callr " << OperandToAssembly(instruction.a, rodata_labels);
        break;
    case OpCode::Jump:
        out << "jmp " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpIfFalse:
        out << "jif " << OperandToAssembly(instruction.a, rodata_labels) << ", "
            << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpCarry:
        out << "jc " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpNotCarry:
        out << "jnc " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpZero:
        out << "jz " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpNotZero:
        out << "jnz " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpSign:
        out << "js " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpNotSign:
        out << "jns " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpOverflow:
        out << "jo " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpNotOverflow:
        out << "jno " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpAbove:
        out << "ja " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpAboveEqual:
        out << "jae " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpBelow:
        out << "jb " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpBelowEqual:
        out << "jbe " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpGreater:
        out << "jg " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpGreaterEqual:
        out << "jge " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpLess:
        out << "jl " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::JumpLessEqual:
        out << "jle " << ResolveTargetLabel(instruction.target, jump_labels);
        break;
    case OpCode::Return:
        out << "ret";
        break;
    }

    return out.str();
}

void WriteInstruction(
    std::ostream &out, const Instruction &instruction,
    const std::unordered_map<std::string, std::uint32_t> &rodata_index_by_text) {
    WriteU8(out, static_cast<std::uint8_t>(instruction.opcode));
    switch (instruction.opcode) {
    case OpCode::Nop:
        break;
    case OpCode::Load:
        WriteU8(out, static_cast<std::uint8_t>(instruction.load_mode));
        WriteU32(out, instruction.dst);
        WriteU32(out, instruction.dst_slot);
        switch (instruction.load_mode) {
        case LoadMode::StackRelative:
        case LoadMode::StackAbsolute:
            WriteU32(out, instruction.slot);
            break;
        case LoadMode::Global:
            WriteString(out, instruction.text);
            break;
        case LoadMode::ArrayElement:
        case LoadMode::ObjectOffset:
            WriteOperand(out, instruction.a, rodata_index_by_text);
            WriteOperand(out, instruction.b, rodata_index_by_text);
            break;
        case LoadMode::FieldOffsetByName:
        case LoadMode::MethodSlotByName:
            WriteOperand(out, instruction.a, rodata_index_by_text);
            WriteString(out, instruction.text);
            break;
        case LoadMode::MethodFunctionBySlot:
            WriteOperand(out, instruction.a, rodata_index_by_text);
            WriteOperand(out, instruction.b, rodata_index_by_text);
            break;
        }
        break;
    case OpCode::Store:
        WriteU8(out, static_cast<std::uint8_t>(instruction.store_mode));
        switch (instruction.store_mode) {
        case StoreMode::StackRelative:
        case StoreMode::StackAbsolute:
            WriteU32(out, instruction.slot);
            WriteOperand(out, instruction.a, rodata_index_by_text);
            break;
        case StoreMode::Global:
            WriteString(out, instruction.text);
            WriteOperand(out, instruction.a, rodata_index_by_text);
            break;
        case StoreMode::ArrayElement:
        case StoreMode::ObjectOffset:
            WriteOperand(out, instruction.a, rodata_index_by_text);
            WriteOperand(out, instruction.b, rodata_index_by_text);
            WriteOperand(out, instruction.c, rodata_index_by_text);
            break;
        }
        break;
    case OpCode::Push:
        WriteOperand(out, instruction.a, rodata_index_by_text);
        break;
    case OpCode::Pop:
        WriteU32(out, instruction.dst);
        WriteU32(out, instruction.dst_slot);
        break;
    case OpCode::DeclareGlobal:
        WriteString(out, instruction.text);
        WriteString(out, instruction.text2);
        WriteOperand(out, instruction.a, rodata_index_by_text);
        break;
    case OpCode::Move:
        WriteU32(out, instruction.dst);
        WriteU32(out, instruction.dst_slot);
        WriteOperand(out, instruction.a, rodata_index_by_text);
        break;
    case OpCode::Unary:
        WriteU32(out, instruction.dst);
        WriteU32(out, instruction.dst_slot);
        WriteU8(out, static_cast<std::uint8_t>(instruction.unary_op));
        WriteOperand(out, instruction.a, rodata_index_by_text);
        break;
    case OpCode::Binary:
        WriteU32(out, instruction.dst);
        WriteU32(out, instruction.dst_slot);
        WriteU8(out, static_cast<std::uint8_t>(instruction.binary_op));
        WriteOperand(out, instruction.a, rodata_index_by_text);
        WriteOperand(out, instruction.b, rodata_index_by_text);
        break;
    case OpCode::Compare:
        WriteOperand(out, instruction.a, rodata_index_by_text);
        WriteOperand(out, instruction.b, rodata_index_by_text);
        break;
    case OpCode::MakeArray:
        WriteU32(out, instruction.dst);
        WriteU32(out, instruction.dst_slot);
        WriteOperandVector(out, instruction.operands, rodata_index_by_text);
        break;
    case OpCode::StackAllocObject:
        WriteU32(out, instruction.dst);
        WriteU32(out, instruction.dst_slot);
        WriteString(out, instruction.text);
        break;
    case OpCode::Call:
        WriteU32(out, instruction.target);
        WriteU32(out, instruction.slot);
        break;
    case OpCode::CallRegister:
        WriteOperand(out, instruction.a, rodata_index_by_text);
        WriteU32(out, instruction.slot);
        break;
    case OpCode::Jump:
    case OpCode::JumpCarry:
    case OpCode::JumpNotCarry:
    case OpCode::JumpZero:
    case OpCode::JumpNotZero:
    case OpCode::JumpSign:
    case OpCode::JumpNotSign:
    case OpCode::JumpOverflow:
    case OpCode::JumpNotOverflow:
    case OpCode::JumpAbove:
    case OpCode::JumpAboveEqual:
    case OpCode::JumpBelow:
    case OpCode::JumpBelowEqual:
    case OpCode::JumpGreater:
    case OpCode::JumpGreaterEqual:
    case OpCode::JumpLess:
    case OpCode::JumpLessEqual:
        WriteU32(out, instruction.target);
        break;
    case OpCode::JumpIfFalse:
        WriteOperand(out, instruction.a, rodata_index_by_text);
        WriteU32(out, instruction.target);
        break;
    case OpCode::Return:
        break;
    }
}

Instruction ReadInstruction(std::istream &in,
                            const std::vector<std::string> &rodata) {
    Instruction instruction;
    instruction.opcode = static_cast<OpCode>(ReadU8(in));
    switch (instruction.opcode) {
    case OpCode::Nop:
        break;
    case OpCode::Load:
        instruction.load_mode = static_cast<LoadMode>(ReadU8(in));
        instruction.dst = ReadU32(in);
        instruction.dst_slot = ReadU32(in);
        switch (instruction.load_mode) {
        case LoadMode::StackRelative:
        case LoadMode::StackAbsolute:
            instruction.slot = ReadU32(in);
            break;
        case LoadMode::Global:
            instruction.text = ReadString(in);
            break;
        case LoadMode::ArrayElement:
        case LoadMode::ObjectOffset:
            instruction.a = ReadOperand(in, rodata);
            instruction.b = ReadOperand(in, rodata);
            break;
        case LoadMode::FieldOffsetByName:
        case LoadMode::MethodSlotByName:
            instruction.a = ReadOperand(in, rodata);
            instruction.text = ReadString(in);
            break;
        case LoadMode::MethodFunctionBySlot:
            instruction.a = ReadOperand(in, rodata);
            instruction.b = ReadOperand(in, rodata);
            break;
        }
        break;
    case OpCode::Store:
        instruction.store_mode = static_cast<StoreMode>(ReadU8(in));
        switch (instruction.store_mode) {
        case StoreMode::StackRelative:
        case StoreMode::StackAbsolute:
            instruction.slot = ReadU32(in);
            instruction.a = ReadOperand(in, rodata);
            break;
        case StoreMode::Global:
            instruction.text = ReadString(in);
            instruction.a = ReadOperand(in, rodata);
            break;
        case StoreMode::ArrayElement:
        case StoreMode::ObjectOffset:
            instruction.a = ReadOperand(in, rodata);
            instruction.b = ReadOperand(in, rodata);
            instruction.c = ReadOperand(in, rodata);
            break;
        }
        break;
    case OpCode::Push:
        instruction.a = ReadOperand(in, rodata);
        break;
    case OpCode::Pop:
        instruction.dst = ReadU32(in);
        instruction.dst_slot = ReadU32(in);
        break;
    case OpCode::DeclareGlobal:
        instruction.text = ReadString(in);
        instruction.text2 = ReadString(in);
        instruction.a = ReadOperand(in, rodata);
        break;
    case OpCode::Move:
        instruction.dst = ReadU32(in);
        instruction.dst_slot = ReadU32(in);
        instruction.a = ReadOperand(in, rodata);
        break;
    case OpCode::Unary:
        instruction.dst = ReadU32(in);
        instruction.dst_slot = ReadU32(in);
        instruction.unary_op = static_cast<ir::UnaryOp>(ReadU8(in));
        instruction.a = ReadOperand(in, rodata);
        break;
    case OpCode::Binary:
        instruction.dst = ReadU32(in);
        instruction.dst_slot = ReadU32(in);
        instruction.binary_op = static_cast<ir::BinaryOp>(ReadU8(in));
        instruction.a = ReadOperand(in, rodata);
        instruction.b = ReadOperand(in, rodata);
        break;
    case OpCode::Compare:
        instruction.a = ReadOperand(in, rodata);
        instruction.b = ReadOperand(in, rodata);
        break;
    case OpCode::MakeArray:
        instruction.dst = ReadU32(in);
        instruction.dst_slot = ReadU32(in);
        instruction.operands = ReadOperandVector(in, rodata);
        break;
    case OpCode::StackAllocObject:
        instruction.dst = ReadU32(in);
        instruction.dst_slot = ReadU32(in);
        instruction.text = ReadString(in);
        break;
    case OpCode::Call:
        instruction.target = ReadU32(in);
        instruction.slot = ReadU32(in);
        break;
    case OpCode::CallRegister:
        instruction.a = ReadOperand(in, rodata);
        instruction.slot = ReadU32(in);
        break;
    case OpCode::Jump:
    case OpCode::JumpCarry:
    case OpCode::JumpNotCarry:
    case OpCode::JumpZero:
    case OpCode::JumpNotZero:
    case OpCode::JumpSign:
    case OpCode::JumpNotSign:
    case OpCode::JumpOverflow:
    case OpCode::JumpNotOverflow:
    case OpCode::JumpAbove:
    case OpCode::JumpAboveEqual:
    case OpCode::JumpBelow:
    case OpCode::JumpBelowEqual:
    case OpCode::JumpGreater:
    case OpCode::JumpGreaterEqual:
    case OpCode::JumpLess:
    case OpCode::JumpLessEqual:
        instruction.target = ReadU32(in);
        break;
    case OpCode::JumpIfFalse:
        instruction.a = ReadOperand(in, rodata);
        instruction.target = ReadU32(in);
        break;
    case OpCode::Return:
        break;
    }
    return instruction;
}

void WriteClassInfo(std::ostream &out, const ClassInfo &class_info) {
    WriteString(out, class_info.name);
    WriteString(out, class_info.base_class);
    WriteStringVector(out, class_info.fields);
    WriteStringVector(out, class_info.field_types);
    WriteStringU32Map(out, class_info.field_offsets);
    WriteStringU32Map(out, class_info.method_slots);
    WriteStringVector(out, class_info.vtable_functions);
    WriteStringStringMap(out, class_info.method_functions);
    WriteString(out, class_info.constructor_function);
}

ClassInfo ReadClassInfo(std::istream &in) {
    ClassInfo class_info;
    class_info.name = ReadString(in);
    class_info.base_class = ReadString(in);
    class_info.fields = ReadStringVector(in);
    class_info.field_types = ReadStringVector(in);
    class_info.field_offsets = ReadStringU32Map(in);
    class_info.method_slots = ReadStringU32Map(in);
    class_info.vtable_functions = ReadStringVector(in);
    class_info.method_functions = ReadStringStringMap(in);
    class_info.constructor_function = ReadString(in);
    return class_info;
}

void WriteGlobalVariable(
    std::ostream &out, const GlobalVariable &global,
    const std::unordered_map<std::string, std::uint32_t> &rodata_index_by_text) {
    WriteString(out, global.name);
    WriteString(out, global.type_name);
    WriteOperand(out, global.value, rodata_index_by_text);
}

GlobalVariable ReadGlobalVariable(std::istream &in,
                                  const std::vector<std::string> &rodata) {
    GlobalVariable global;
    global.name = ReadString(in);
    global.type_name = ReadString(in);
    global.value = ReadOperand(in, rodata);
    return global;
}

void WriteFunction(
    std::ostream &out, const Function &function,
    const std::unordered_map<std::string, std::uint32_t> &rodata_index_by_text) {
    WriteString(out, function.name);
    WriteStringVector(out, function.params);
    WriteStringVector(out, function.param_types);
    WriteVectorScalar(out, function.param_slots);
    WriteStringVector(out, function.slot_names);
    WriteStringVector(out, function.slot_types);
    WriteU32(out, function.entry_pc);
    WriteU32(out, function.register_count);
    WriteU32(out, function.local_count);
    WriteU32(out, function.stack_slot_count);
    WriteU8(out, function.is_method ? 1U : 0U);
    WriteString(out, function.owning_class);
    WriteString(out, function.method_name);

    WriteU64(out, function.code.size());
    for (const Instruction &instruction : function.code) {
        WriteInstruction(out, instruction, rodata_index_by_text);
    }
}

Function ReadFunction(std::istream &in, const std::vector<std::string> &rodata) {
    Function function;
    function.name = ReadString(in);
    function.params = ReadStringVector(in);
    function.param_types = ReadStringVector(in);
    function.param_slots = ReadVectorScalar<SlotId>(in);
    function.slot_names = ReadStringVector(in);
    function.slot_types = ReadStringVector(in);
    function.entry_pc = ReadU32(in);
    function.register_count = ReadU32(in);
    function.local_count = ReadU32(in);
    function.stack_slot_count = ReadU32(in);
    function.is_method = ReadU8(in) != 0;
    function.owning_class = ReadString(in);
    function.method_name = ReadString(in);

    const std::uint64_t code_count = ReadU64(in);
    if (code_count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("code length exceeds platform limits");
    }
    function.code.reserve(code_count);
    for (std::uint64_t i = 0; i < code_count; ++i) {
        function.code.push_back(ReadInstruction(in, rodata));
    }
    return function;
}

void WriteProgram(std::ostream &out, const Program &program) {
    WriteString(out, program.entry_function);

    WriteU64(out, program.classes.size());
    for (const ClassInfo &class_info : program.classes) {
        WriteClassInfo(out, class_info);
    }

    const std::vector<std::string> rodata = BuildProgramRodata(program);
    WriteStringVector(out, rodata);
    std::unordered_map<std::string, std::uint32_t> rodata_index_by_text;
    rodata_index_by_text.reserve(rodata.size());
    for (std::uint32_t i = 0; i < rodata.size(); ++i) {
        rodata_index_by_text.emplace(rodata[i], i);
    }

    WriteU64(out, program.globals.size());
    for (const GlobalVariable &global : program.globals) {
        WriteGlobalVariable(out, global, rodata_index_by_text);
    }

    WriteU64(out, program.functions.size());
    for (const Function &function : program.functions) {
        WriteFunction(out, function, rodata_index_by_text);
    }
}

Program ReadProgram(std::istream &in) {
    Program program;
    program.entry_function = ReadString(in);

    const std::uint64_t class_count = ReadU64(in);
    if (class_count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("class count exceeds platform limits");
    }
    program.classes.reserve(class_count);
    for (std::uint64_t i = 0; i < class_count; ++i) {
        program.classes.push_back(ReadClassInfo(in));
    }

    const std::vector<std::string> rodata = ReadStringVector(in);

    const std::uint64_t global_count = ReadU64(in);
    if (global_count > std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("global count exceeds platform limits");
    }
    program.globals.reserve(global_count);
    for (std::uint64_t i = 0; i < global_count; ++i) {
        program.globals.push_back(ReadGlobalVariable(in, rodata));
    }

    const std::uint64_t function_count = ReadU64(in);
    if (function_count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("function count exceeds platform limits");
    }
    program.functions.reserve(function_count);
    for (std::uint64_t i = 0; i < function_count; ++i) {
        program.functions.push_back(ReadFunction(in, rodata));
    }

    return program;
}

} // namespace

namespace {

std::string CanonicalizePassName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char ch : name) {
        const unsigned char raw = static_cast<unsigned char>(ch);
        if (std::isspace(raw) != 0 || ch == '-' || ch == '_') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(raw)));
    }
    if (out.size() > 4 && out.substr(out.size() - 4) == "pass") {
        out.resize(out.size() - 4);
    }
    return out;
}

bool IsEntryFunction(const Program &program, const Function &function) {
    return function.name == program.entry_function;
}

Address ResolveBuiltinAddressLocal(const std::string_view name) {
    for (std::size_t i = 0; i < kBuiltinFunctionNames.size(); ++i) {
        if (kBuiltinFunctionNames[i] == name) {
            return kBuiltinAddressBase + static_cast<Address>(i);
        }
    }
    return kInvalidAddress;
}

std::unordered_map<std::string, Address>
BuildFunctionAddressTable(const ProgramBundle &bundle) {
    std::unordered_map<std::string, Address> address_by_name;
    Address next_address = 0;
    const auto assign_program = [&](const Program &program) {
        for (const Function &function : program.functions) {
            if (IsEntryFunction(program, function)) {
                continue;
            }
            if (next_address >= kBuiltinAddressBase) {
                throw BytecodeException(
                    "user function address space exhausted while assembling");
            }
            if (address_by_name.contains(function.name)) {
                throw BytecodeException("duplicate function symbol while "
                                        "assembling bytecode: " +
                                        function.name);
            }
            address_by_name.emplace(function.name, next_address++);
        }
    };

    for (const ProgramUnit &unit : bundle.prelude_units) {
        assign_program(unit.program);
    }
    assign_program(bundle.program);
    return address_by_name;
}

void AssembleCallsInProgram(
    Program &program, const std::string_view unit_name,
    const std::unordered_map<std::string, Address> &function_address_by_name) {
    for (Function &function : program.functions) {
        for (Instruction &inst : function.code) {
            if (inst.opcode == OpCode::Call) {
                if (!inst.operands.empty()) {
                    throw BytecodeException(
                        "call still contains inline arguments after lowering in '" +
                        function.name +
                        "'; call must use argument-lowered form");
                }
                if (inst.text.empty()) {
                    continue;
                }

                const auto user_target = function_address_by_name.find(inst.text);
                if (user_target != function_address_by_name.end()) {
                    inst.target = user_target->second;
                } else {
                    const Address builtin_target =
                        ResolveBuiltinAddressLocal(inst.text);
                    if (builtin_target == kInvalidAddress) {
                        throw BytecodeException(
                            "unknown call target while assembling bytecode: '" +
                            inst.text + "' in function '" + function.name +
                            "' (" + std::string(unit_name) + ")");
                    }
                    inst.target = builtin_target;
                }
                inst.text.clear();
                inst.dst = kInvalidRegister;
                inst.dst_slot = kInvalidSlot;
                continue;
            }

            if (inst.opcode == OpCode::CallRegister) {
                if (!inst.operands.empty()) {
                    throw BytecodeException(
                        "callr still contains inline arguments after lowering in '" +
                        function.name +
                        "'; callr must use argument-lowered form");
                }
                inst.dst = kInvalidRegister;
                inst.dst_slot = kInvalidSlot;
                continue;
            }

            if (inst.opcode == OpCode::Return) {
                if (inst.a.kind == OperandKind::Null) {
                    continue;
                }
                if (inst.a.kind == OperandKind::Register && inst.a.reg == 0) {
                    inst.a = Operand::Null();
                    continue;
                }
                throw BytecodeException(
                    "return still carries a value operand after return lowering "
                    "in function '" +
                    function.name + "'");
            }
        }
    }
}

void AssembleProgramBundleInPlace(ProgramBundle &bundle) {
    const std::unordered_map<std::string, Address> function_address_by_name =
        BuildFunctionAddressTable(bundle);
    for (ProgramUnit &unit : bundle.prelude_units) {
        AssembleCallsInProgram(unit.program, unit.name, function_address_by_name);
    }
    AssembleCallsInProgram(bundle.program, "<program>", function_address_by_name);
}

} // namespace

Address BuiltinAddressForName(const std::string_view name) {
    for (std::size_t i = 0; i < kBuiltinFunctionNames.size(); ++i) {
        if (kBuiltinFunctionNames[i] == name) {
            return kBuiltinAddressBase + static_cast<Address>(i);
        }
    }
    return kInvalidAddress;
}

std::string_view BuiltinNameForAddress(const Address address) {
    if (address < kBuiltinAddressBase) {
        return {};
    }
    const Address index = address - kBuiltinAddressBase;
    if (index >= kBuiltinFunctionNames.size()) {
        return {};
    }
    return kBuiltinFunctionNames[index];
}

Program LowerIRProgram(const ir::Program &program) {
    Program out;
    out.entry_function = program.entry_function;

    out.classes.reserve(program.classes.size());
    for (const ir::ClassInfo &class_info : program.classes) {
        out.classes.push_back(LowerClassInfo(class_info));
    }

    out.functions.reserve(program.functions.size());
    for (const ir::Function &function : program.functions) {
        std::vector<GlobalVariable> *globals = nullptr;
        if (function.name == program.entry_function) {
            globals = &out.globals;
        }
        out.functions.push_back(LowerFunction(function, globals));
    }

    OptimizeProgram(out);
    return out;
}

ProgramBundle LowerIRBundle(const ir::ProgramBundle &bundle) {
    ProgramBundle out;
    out.program = LowerIRProgram(bundle.program);
    out.prelude_units.reserve(bundle.prelude_units.size());
    for (const ir::ProgramUnit &unit : bundle.prelude_units) {
        out.prelude_units.push_back(ProgramUnit{
            .name = unit.name, .program = LowerIRProgram(unit.program)});
    }
    AssembleProgramBundleInPlace(out);
    return out;
}

std::vector<std::string> ListOptimizationPasses() {
    std::vector<std::string> names;
    const std::vector<std::unique_ptr<Pass>> passes = BuildDefaultPassPipeline();
    names.reserve(passes.size());
    for (const std::unique_ptr<Pass> &pass : passes) {
        names.emplace_back(pass->Name());
    }
    return names;
}

void OptimizeProgram(Program &program, const OptimizationOptions &options) {
    const std::vector<std::unique_ptr<Pass>> passes = BuildDefaultPassPipeline();

    std::unordered_map<std::string, std::string> canonical_to_name;
    canonical_to_name.reserve(passes.size());
    for (const std::unique_ptr<Pass> &pass : passes) {
        canonical_to_name.emplace(CanonicalizePassName(pass->Name()),
                                  std::string(pass->Name()));
    }

    std::unordered_set<std::string> disabled;
    disabled.reserve(options.disabled_passes.size());
    for (const std::string &pass_name : options.disabled_passes) {
        const std::string canonical = CanonicalizePassName(pass_name);
        const auto found = canonical_to_name.find(canonical);
        if (found == canonical_to_name.end()) {
            std::string available;
            for (const std::unique_ptr<Pass> &pass : passes) {
                if (!available.empty()) {
                    available += ", ";
                }
                available += std::string(pass->Name());
            }
            throw BytecodeException("unknown bytecode optimization pass '" +
                                    pass_name + "'. Available passes: " +
                                    available);
        }
        disabled.insert(canonical);
    }

    const int max_rounds = std::max(1, options.max_rounds);
    for (int round = 0; round < max_rounds; ++round) {
        bool changed = false;
        for (const std::unique_ptr<Pass> &pass : passes) {
            if (pass->RunOnce() && round > 0) {
                continue;
            }
            if (disabled.contains(CanonicalizePassName(pass->Name()))) {
                continue;
            }
            for (Function &function : program.functions) {
                changed |= pass->Run(function);
            }
        }
        if (!changed) {
            break;
        }
    }
}

void OptimizeProgram(Program &program) {
    OptimizeProgram(program, OptimizationOptions{});
}

std::string ProgramToAssembly(const Program &program,
                              const std::string_view unit_name) {
    std::ostringstream out;
    out << "; bytecode unit: " << unit_name << "\n";
    const std::vector<std::string> rodata = BuildProgramRodata(program);
    const RodataLabelMap rodata_labels = BuildRodataLabelMap(rodata);

    if (!rodata.empty()) {
        out << "; rodata\n";
        for (std::size_t i = 0; i < rodata.size(); ++i) {
            out << BuildIndexedLabel("ro_data", i) << ": \""
                << EscapeString(rodata[i]) << "\"\n";
        }
        out << "\n";
    }

    if (!program.globals.empty()) {
        out << "; globals\n";
        for (const GlobalVariable &global : program.globals) {
            out << "global @" << global.name;
            if (!global.type_name.empty()) {
                out << ":" << global.type_name;
            }
            out << ", " << OperandToAssembly(global.value, &rodata_labels)
                << "\n";
        }
        out << "\n";
    }

    if (!program.classes.empty()) {
        out << "; classes\n";
        for (const ClassInfo &class_info : program.classes) {
            out << "; class " << class_info.name;
            if (!class_info.base_class.empty()) {
                out << " : " << class_info.base_class;
            }
            out << " fields=";
            for (std::size_t i = 0; i < class_info.fields.size(); ++i) {
                if (i != 0) {
                    out << ",";
                }
                out << class_info.fields[i] << "@"
                    << class_info.field_offsets.at(class_info.fields[i]);
                if (i < class_info.field_types.size() &&
                    !class_info.field_types[i].empty()) {
                    out << ":" << class_info.field_types[i];
                }
            }
            out << "\n";
            if (!class_info.constructor_function.empty()) {
                out << ";   ctor -> " << class_info.constructor_function << "\n";
            }
            for (std::size_t slot = 0;
                 slot < class_info.vtable_functions.size(); ++slot) {
                std::string method_name;
                for (const auto &[candidate_name, candidate_slot] :
                     class_info.method_slots) {
                    if (candidate_slot == slot) {
                        method_name = candidate_name;
                        break;
                    }
                }
                out << ";   vslot " << slot;
                if (!method_name.empty()) {
                    out << " " << method_name;
                }
                out << " -> " << class_info.vtable_functions[slot] << "\n";
            }
        }
        out << "\n";
    }

    out << "; code\n";
    for (std::size_t function_index = 0; function_index < program.functions.size();
         ++function_index) {
        const Function &function = program.functions[function_index];
        const std::string function_label =
            FunctionLabelForAssembly(function.name, 0, function_index);

        out << function_label << ":\n";
        out << "    ; symbol " << function.name;
        if (function.is_method) {
            out << " (method " << function.owning_class << "."
                << function.method_name << ")";
        }
        out << "\n";

        AddressLabelMap jump_labels;
        std::unordered_set<Address> used_labels;
        used_labels.insert(function.entry_pc);
        for (const Instruction &inst : function.code) {
            switch (inst.opcode) {
            case OpCode::Jump:
            case OpCode::JumpIfFalse:
            case OpCode::JumpCarry:
            case OpCode::JumpNotCarry:
            case OpCode::JumpZero:
            case OpCode::JumpNotZero:
            case OpCode::JumpSign:
            case OpCode::JumpNotSign:
            case OpCode::JumpOverflow:
            case OpCode::JumpNotOverflow:
            case OpCode::JumpAbove:
            case OpCode::JumpAboveEqual:
            case OpCode::JumpBelow:
            case OpCode::JumpBelowEqual:
            case OpCode::JumpGreater:
            case OpCode::JumpGreaterEqual:
            case OpCode::JumpLess:
            case OpCode::JumpLessEqual:
                used_labels.insert(inst.target);
                break;
            default:
                break;
            }
        }
        jump_labels.reserve(used_labels.size());
        for (const Address target : used_labels) {
            jump_labels.emplace(target,
                                JumpLabelForAssembly(function_label, target));
        }

        for (std::size_t pc = 0; pc < function.code.size(); ++pc) {
            const auto found = jump_labels.find(static_cast<Address>(pc));
            if (found != jump_labels.end()) {
                out << found->second << ":\n";
            }
            out << "    " << InstructionToAssembly(function.code[pc], &rodata_labels,
                                                   &jump_labels, nullptr)
                << "\n";
        }
        out << "\n";
    }

    return out.str();
}

std::string ProgramBundleToAssembly(const ProgramBundle &bundle,
                                    const std::string_view main_unit_name) {
    std::ostringstream out;
    out << "; bytecode bundle\n";

    const std::vector<std::string> rodata = BuildBundleRodata(bundle);
    const RodataLabelMap rodata_labels = BuildRodataLabelMap(rodata);
    if (!rodata.empty()) {
        out << "; rodata\n";
        for (std::size_t i = 0; i < rodata.size(); ++i) {
            out << BuildIndexedLabel("ro_data", i) << ": \""
                << EscapeString(rodata[i]) << "\"\n";
        }
        out << "\n";
    }

    std::unordered_map<std::string, std::string> function_labels_by_name;
    const auto assign_function_labels = [&](const Program &program,
                                            const std::size_t unit_index) {
        for (std::size_t function_index = 0;
             function_index < program.functions.size(); ++function_index) {
            const Function &function = program.functions[function_index];
            const std::string label = FunctionLabelForAssembly(
                function.name, unit_index, function_index);
            if (!function_labels_by_name.contains(function.name)) {
                function_labels_by_name.emplace(function.name, label);
            }
        }
    };
    for (std::size_t unit_index = 0; unit_index < bundle.prelude_units.size();
         ++unit_index) {
        assign_function_labels(bundle.prelude_units[unit_index].program, unit_index);
    }
    const std::size_t main_unit_index = bundle.prelude_units.size();
    assign_function_labels(bundle.program, main_unit_index);

    AddressLabelMap call_labels;
    const std::unordered_map<std::string, Address> function_address_by_name =
        BuildFunctionAddressTable(bundle);
    call_labels.reserve(function_address_by_name.size() + kBuiltinFunctionNames.size());
    for (const auto &[name, address] : function_address_by_name) {
        const auto label_it = function_labels_by_name.find(name);
        if (label_it != function_labels_by_name.end()) {
            call_labels.emplace(address, label_it->second);
        }
    }
    for (std::size_t i = 0; i < kBuiltinFunctionNames.size(); ++i) {
        const Address address = kBuiltinAddressBase + static_cast<Address>(i);
        call_labels.emplace(
            address, "builtin_" +
                         common::SanitizeIdentifier(kBuiltinFunctionNames[i],
                                                    "builtin"));
    }

    const auto append_program = [&](const Program &program,
                                    const std::string_view unit_name,
                                    const std::size_t unit_index) {
        out << "; unit " << unit_name << "\n";

        if (!program.globals.empty()) {
            out << "; globals\n";
            for (const GlobalVariable &global : program.globals) {
                out << "global @" << global.name;
                if (!global.type_name.empty()) {
                    out << ":" << global.type_name;
                }
                out << ", " << OperandToAssembly(global.value, &rodata_labels)
                    << "\n";
            }
            out << "\n";
        }

        if (!program.classes.empty()) {
            out << "; classes\n";
            for (const ClassInfo &class_info : program.classes) {
                out << "; class " << class_info.name;
                if (!class_info.base_class.empty()) {
                    out << " : " << class_info.base_class;
                }
                out << " fields=";
                for (std::size_t i = 0; i < class_info.fields.size(); ++i) {
                    if (i != 0) {
                        out << ",";
                    }
                    out << class_info.fields[i] << "@"
                        << class_info.field_offsets.at(class_info.fields[i]);
                    if (i < class_info.field_types.size() &&
                        !class_info.field_types[i].empty()) {
                        out << ":" << class_info.field_types[i];
                    }
                }
                out << "\n";

                if (!class_info.constructor_function.empty()) {
                    out << ";   ctor -> " << class_info.constructor_function << "\n";
                }

                for (std::size_t slot = 0;
                     slot < class_info.vtable_functions.size(); ++slot) {
                    std::string method_name;
                    for (const auto &[candidate_name, candidate_slot] :
                         class_info.method_slots) {
                        if (candidate_slot == slot) {
                            method_name = candidate_name;
                            break;
                        }
                    }
                    out << ";   vslot " << slot;
                    if (!method_name.empty()) {
                        out << " " << method_name;
                    }
                    out << " -> " << class_info.vtable_functions[slot] << "\n";
                }
            }
        }

        out << "; code\n";
        for (std::size_t function_index = 0;
             function_index < program.functions.size(); ++function_index) {
            const Function &function = program.functions[function_index];
            const std::string function_label = FunctionLabelForAssembly(
                function.name, unit_index, function_index);
            out << function_label << ":\n";
            out << "    ; symbol " << function.name;
            if (function.is_method) {
                out << " (method " << function.owning_class << "."
                    << function.method_name << ")";
            }
            if (function.name == program.entry_function) {
                out << " [entry]";
            }
            out << "\n";

            AddressLabelMap jump_labels;
            std::unordered_set<Address> used_labels;
            used_labels.insert(function.entry_pc);
            for (const Instruction &inst : function.code) {
                switch (inst.opcode) {
                case OpCode::Jump:
                case OpCode::JumpIfFalse:
                case OpCode::JumpCarry:
                case OpCode::JumpNotCarry:
                case OpCode::JumpZero:
                case OpCode::JumpNotZero:
                case OpCode::JumpSign:
                case OpCode::JumpNotSign:
                case OpCode::JumpOverflow:
                case OpCode::JumpNotOverflow:
                case OpCode::JumpAbove:
                case OpCode::JumpAboveEqual:
                case OpCode::JumpBelow:
                case OpCode::JumpBelowEqual:
                case OpCode::JumpGreater:
                case OpCode::JumpGreaterEqual:
                case OpCode::JumpLess:
                case OpCode::JumpLessEqual:
                    used_labels.insert(inst.target);
                    break;
                default:
                    break;
                }
            }
            jump_labels.reserve(used_labels.size());
            for (const Address target : used_labels) {
                jump_labels.emplace(target,
                                    JumpLabelForAssembly(function_label, target));
            }

            for (std::size_t pc = 0; pc < function.code.size(); ++pc) {
                const auto found = jump_labels.find(static_cast<Address>(pc));
                if (found != jump_labels.end()) {
                    out << found->second << ":\n";
                }
                out << "    "
                    << InstructionToAssembly(function.code[pc], &rodata_labels,
                                             &jump_labels, &call_labels)
                    << "\n";
            }
            out << "\n";
        }
    };

    for (std::size_t unit_index = 0; unit_index < bundle.prelude_units.size();
         ++unit_index) {
        append_program(bundle.prelude_units[unit_index].program,
                       bundle.prelude_units[unit_index].name, unit_index);
    }
    append_program(bundle.program, main_unit_name, main_unit_index);
    return out.str();
}

void WriteProgramBundleBinary(const ProgramBundle &bundle,
                              const std::filesystem::path &output_path) {
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw BytecodeException("failed to open output file for bytecode: " +
                                output_path.string());
    }

    WriteBytes(out, kBundleMagic.data(), kBundleMagic.size());
    WriteProgram(out, bundle.program);

    WriteU64(out, bundle.prelude_units.size());
    for (const ProgramUnit &unit : bundle.prelude_units) {
        WriteString(out, unit.name);
        WriteProgram(out, unit.program);
    }

    if (!out.good()) {
        throw BytecodeException("failed while writing bytecode bundle: " +
                                output_path.string());
    }
}

ProgramBundle ReadProgramBundleBinary(const std::filesystem::path &input_path) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        throw BytecodeException("failed to open bytecode bundle: " +
                                input_path.string());
    }

    std::array<char, kBundleMagic.size()> magic{};
    ReadBytes(in, magic.data(), magic.size());
    if (magic != kBundleMagic) {
        throw BytecodeException("invalid bytecode bundle header in file: " +
                                input_path.string());
    }

    ProgramBundle bundle;
    bundle.program = ReadProgram(in);

    const std::uint64_t unit_count = ReadU64(in);
    if (unit_count >
        std::numeric_limits<std::size_t>::max()) {
        throw BytecodeException("prelude unit count exceeds platform limits");
    }

    bundle.prelude_units.reserve(unit_count);
    for (std::uint64_t i = 0; i < unit_count; ++i) {
        ProgramUnit unit;
        unit.name = ReadString(in);
        unit.program = ReadProgram(in);
        bundle.prelude_units.push_back(std::move(unit));
    }

    return bundle;
}

} // namespace compiler::bytecode
