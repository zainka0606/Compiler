#include "IR.h"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace compiler::ir {

namespace {

constexpr std::array kBundleMagic = {'N', 'E', 'O', 'N',
                                              'I', 'R', '0', '2'};

enum class InstructionTag : std::uint8_t {
    LoadLocal = 0,
    StoreLocal = 1,
    DeclareGlobal = 2,
    LoadGlobal = 3,
    StoreGlobal = 4,
    Move = 5,
    Unary = 6,
    Binary = 7,
    MakeArray = 8,
    ArrayLoad = 9,
    ArrayStore = 10,
    ResolveFieldOffset = 11,
    ObjectLoad = 12,
    ObjectStore = 13,
    ResolveMethodSlot = 14,
    Call = 15,
    VirtualCall = 16,
};

enum class TerminatorTag : std::uint8_t {
    Jump = 0,
    Branch = 1,
    Return = 2,
};

void WriteBytes(std::ostream &out, const void *data, const std::size_t size) {
    out.write(static_cast<const char *>(data),
              static_cast<std::streamsize>(size));
    if (!out.good()) {
        throw IRException("failed while writing binary bundle");
    }
}

void ReadBytes(std::istream &in, void *data, const std::size_t size) {
    in.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
    if (!in.good()) {
        throw IRException("unexpected end of binary bundle");
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
        throw IRException("string length exceeds platform limits");
    }

    std::string out(size, '\0');
    if (!out.empty()) {
        ReadBytes(in, out.data(), out.size());
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
        throw IRException("vector length exceeds platform limits");
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
        throw IRException("vector length exceeds platform limits");
    }

    std::vector<std::string> out;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        out.push_back(ReadString(in));
    }
    return out;
}

void WriteValueRef(std::ostream &out, const ValueRef &value) {
    WriteU8(out, static_cast<std::uint8_t>(value.kind));
    switch (value.kind) {
    case ValueKind::Temp:
        WriteU32(out, value.temp);
        break;
    case ValueKind::Number:
        WriteU64(out, std::bit_cast<std::uint64_t>(value.number));
        break;
    case ValueKind::String:
        WriteString(out, value.text);
        break;
    case ValueKind::Bool:
        WriteU8(out, value.boolean ? 1U : 0U);
        break;
    case ValueKind::Char:
        WriteU8(out, static_cast<std::uint8_t>(value.character));
        break;
    case ValueKind::Null:
        break;
    }
}

ValueRef ReadValueRef(std::istream &in) {
    const auto kind = static_cast<ValueKind>(ReadU8(in));
    switch (kind) {
    case ValueKind::Temp:
        return ValueRef::Temp(ReadU32(in));
    case ValueKind::Number:
        return ValueRef::Number(std::bit_cast<double>(ReadU64(in)));
    case ValueKind::String:
        return ValueRef::String(ReadString(in));
    case ValueKind::Bool:
        return ValueRef::Bool(ReadU8(in) != 0);
    case ValueKind::Char:
        return ValueRef::Char(static_cast<char>(ReadU8(in)));
    case ValueKind::Null:
        return ValueRef::Null();
    }
    throw IRException("invalid value kind while decoding bundle");
}

void WriteInstruction(std::ostream &out, const Instruction &instruction) {
    std::visit(
        [&](const auto &inst) {
            using T = std::decay_t<decltype(inst)>;
            if constexpr (std::is_same_v<T, LoadLocalInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::LoadLocal));
                WriteU32(out, inst.dst);
                WriteU32(out, inst.slot);
            } else if constexpr (std::is_same_v<T, StoreLocalInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::StoreLocal));
                WriteU32(out, inst.slot);
                WriteValueRef(out, inst.value);
            } else if constexpr (std::is_same_v<T, DeclareGlobalInst>) {
                WriteU8(out, static_cast<std::uint8_t>(
                                 InstructionTag::DeclareGlobal));
                WriteString(out, inst.name);
                WriteString(out, inst.type_name);
                WriteValueRef(out, inst.value);
            } else if constexpr (std::is_same_v<T, LoadGlobalInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::LoadGlobal));
                WriteU32(out, inst.dst);
                WriteString(out, inst.name);
            } else if constexpr (std::is_same_v<T, StoreGlobalInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::StoreGlobal));
                WriteString(out, inst.name);
                WriteValueRef(out, inst.value);
            } else if constexpr (std::is_same_v<T, MoveInst>) {
                WriteU8(out, static_cast<std::uint8_t>(InstructionTag::Move));
                WriteU32(out, inst.dst);
                WriteValueRef(out, inst.src);
            } else if constexpr (std::is_same_v<T, UnaryInst>) {
                WriteU8(out, static_cast<std::uint8_t>(InstructionTag::Unary));
                WriteU32(out, inst.dst);
                WriteU8(out, static_cast<std::uint8_t>(inst.op));
                WriteValueRef(out, inst.value);
            } else if constexpr (std::is_same_v<T, BinaryInst>) {
                WriteU8(out, static_cast<std::uint8_t>(InstructionTag::Binary));
                WriteU32(out, inst.dst);
                WriteU8(out, static_cast<std::uint8_t>(inst.op));
                WriteValueRef(out, inst.lhs);
                WriteValueRef(out, inst.rhs);
            } else if constexpr (std::is_same_v<T, MakeArrayInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::MakeArray));
                WriteU32(out, inst.dst);
                WriteU64(out, static_cast<std::uint64_t>(inst.elements.size()));
                for (const ValueRef &value : inst.elements) {
                    WriteValueRef(out, value);
                }
            } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::ArrayLoad));
                WriteU32(out, inst.dst);
                WriteValueRef(out, inst.array);
                WriteValueRef(out, inst.index);
            } else if constexpr (std::is_same_v<T, ArrayStoreInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::ArrayStore));
                WriteValueRef(out, inst.array);
                WriteValueRef(out, inst.index);
                WriteValueRef(out, inst.value);
            } else if constexpr (std::is_same_v<T, ResolveFieldOffsetInst>) {
                WriteU8(out, static_cast<std::uint8_t>(
                                 InstructionTag::ResolveFieldOffset));
                WriteU32(out, inst.dst);
                WriteValueRef(out, inst.object);
                WriteString(out, inst.member);
            } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::ObjectLoad));
                WriteU32(out, inst.dst);
                WriteValueRef(out, inst.object);
                WriteValueRef(out, inst.offset);
            } else if constexpr (std::is_same_v<T, ObjectStoreInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::ObjectStore));
                WriteValueRef(out, inst.object);
                WriteValueRef(out, inst.offset);
                WriteValueRef(out, inst.value);
            } else if constexpr (std::is_same_v<T, ResolveMethodSlotInst>) {
                WriteU8(out, static_cast<std::uint8_t>(
                                 InstructionTag::ResolveMethodSlot));
                WriteU32(out, inst.dst);
                WriteValueRef(out, inst.object);
                WriteString(out, inst.method);
            } else if constexpr (std::is_same_v<T, CallInst>) {
                WriteU8(out, static_cast<std::uint8_t>(InstructionTag::Call));
                WriteU32(out, inst.dst);
                WriteString(out, inst.callee);
                WriteU64(out, static_cast<std::uint64_t>(inst.args.size()));
                for (const ValueRef &arg : inst.args) {
                    WriteValueRef(out, arg);
                }
            } else if constexpr (std::is_same_v<T, VirtualCallInst>) {
                WriteU8(out,
                        static_cast<std::uint8_t>(InstructionTag::VirtualCall));
                WriteU32(out, inst.dst);
                WriteValueRef(out, inst.object);
                WriteValueRef(out, inst.slot);
                WriteU64(out, static_cast<std::uint64_t>(inst.args.size()));
                for (const ValueRef &arg : inst.args) {
                    WriteValueRef(out, arg);
                }
            }
        },
        instruction);
}

Instruction ReadInstruction(std::istream &in) {
    const auto tag = static_cast<InstructionTag>(ReadU8(in));
    switch (tag) {
    case InstructionTag::LoadLocal:
        return LoadLocalInst{.dst = ReadU32(in), .slot = ReadU32(in)};
    case InstructionTag::StoreLocal:
        return StoreLocalInst{.slot = ReadU32(in), .value = ReadValueRef(in)};
    case InstructionTag::DeclareGlobal:
        return DeclareGlobalInst{
            .name = ReadString(in),
            .type_name = ReadString(in),
            .value = ReadValueRef(in),
        };
    case InstructionTag::LoadGlobal:
        return LoadGlobalInst{.dst = ReadU32(in), .name = ReadString(in)};
    case InstructionTag::StoreGlobal:
        return StoreGlobalInst{.name = ReadString(in),
                               .value = ReadValueRef(in)};
    case InstructionTag::Move:
        return MoveInst{.dst = ReadU32(in), .src = ReadValueRef(in)};
    case InstructionTag::Unary:
        return UnaryInst{.dst = ReadU32(in),
                         .op = static_cast<UnaryOp>(ReadU8(in)),
                         .value = ReadValueRef(in)};
    case InstructionTag::Binary:
        return BinaryInst{.dst = ReadU32(in),
                          .op = static_cast<BinaryOp>(ReadU8(in)),
                          .lhs = ReadValueRef(in),
                          .rhs = ReadValueRef(in)};
    case InstructionTag::MakeArray: {
        MakeArrayInst out;
        out.dst = ReadU32(in);
        const std::uint64_t count = ReadU64(in);
        if (count > std::numeric_limits<std::size_t>::max()) {
            throw IRException("array length exceeds platform limits");
        }
        out.elements.reserve(count);
        for (std::uint64_t i = 0; i < count; ++i) {
            out.elements.push_back(ReadValueRef(in));
        }
        return out;
    }
    case InstructionTag::ArrayLoad:
        return ArrayLoadInst{.dst = ReadU32(in),
                             .array = ReadValueRef(in),
                             .index = ReadValueRef(in)};
    case InstructionTag::ArrayStore:
        return ArrayStoreInst{.array = ReadValueRef(in),
                              .index = ReadValueRef(in),
                              .value = ReadValueRef(in)};
    case InstructionTag::ResolveFieldOffset:
        return ResolveFieldOffsetInst{.dst = ReadU32(in),
                                      .object = ReadValueRef(in),
                                      .member = ReadString(in)};
    case InstructionTag::ObjectLoad:
        return ObjectLoadInst{.dst = ReadU32(in),
                              .object = ReadValueRef(in),
                              .offset = ReadValueRef(in)};
    case InstructionTag::ObjectStore:
        return ObjectStoreInst{.object = ReadValueRef(in),
                               .offset = ReadValueRef(in),
                               .value = ReadValueRef(in)};
    case InstructionTag::ResolveMethodSlot:
        return ResolveMethodSlotInst{.dst = ReadU32(in),
                                     .object = ReadValueRef(in),
                                     .method = ReadString(in)};
    case InstructionTag::Call: {
        CallInst out;
        out.dst = ReadU32(in);
        out.callee = ReadString(in);
        const std::uint64_t count = ReadU64(in);
        if (count > std::numeric_limits<std::size_t>::max()) {
            throw IRException("call arg count exceeds platform limits");
        }
        out.args.reserve(count);
        for (std::uint64_t i = 0; i < count; ++i) {
            out.args.push_back(ReadValueRef(in));
        }
        return out;
    }
    case InstructionTag::VirtualCall: {
        VirtualCallInst out;
        out.dst = ReadU32(in);
        out.object = ReadValueRef(in);
        out.slot = ReadValueRef(in);
        const std::uint64_t count = ReadU64(in);
        if (count > std::numeric_limits<std::size_t>::max()) {
            throw IRException("virtual call arg count exceeds platform limits");
        }
        out.args.reserve(count);
        for (std::uint64_t i = 0; i < count; ++i) {
            out.args.push_back(ReadValueRef(in));
        }
        return out;
    }
    }
    throw IRException("invalid instruction tag while decoding bundle");
}

void WriteTerminator(std::ostream &out, const Terminator &terminator) {
    std::visit(
        [&](const auto &term) {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, JumpTerm>) {
                WriteU8(out, static_cast<std::uint8_t>(TerminatorTag::Jump));
                WriteU32(out, term.target);
            } else if constexpr (std::is_same_v<T, BranchTerm>) {
                WriteU8(out, static_cast<std::uint8_t>(TerminatorTag::Branch));
                WriteValueRef(out, term.condition);
                WriteU32(out, term.true_target);
                WriteU32(out, term.false_target);
            } else if constexpr (std::is_same_v<T, ReturnTerm>) {
                WriteU8(out, static_cast<std::uint8_t>(TerminatorTag::Return));
                WriteU8(out, term.value.has_value() ? 1U : 0U);
                if (term.value.has_value()) {
                    WriteValueRef(out, *term.value);
                }
            }
        },
        terminator);
}

Terminator ReadTerminator(std::istream &in) {
    const auto tag = static_cast<TerminatorTag>(ReadU8(in));
    switch (tag) {
    case TerminatorTag::Jump:
        return JumpTerm{.target = ReadU32(in)};
    case TerminatorTag::Branch:
        return BranchTerm{.condition = ReadValueRef(in),
                          .true_target = ReadU32(in),
                          .false_target = ReadU32(in)};
    case TerminatorTag::Return: {
        ReturnTerm out;
        const bool has_value = ReadU8(in) != 0;
        if (has_value) {
            out.value = ReadValueRef(in);
        }
        return out;
    }
    }
    throw IRException("invalid terminator tag while decoding bundle");
}

void WriteBasicBlock(std::ostream &out, const BasicBlock &block) {
    WriteU32(out, block.id);
    WriteString(out, block.label);
    WriteU64(out, block.instructions.size());
    for (const Instruction &instruction : block.instructions) {
        WriteInstruction(out, instruction);
    }
    WriteU8(out, block.terminator.has_value() ? 1U : 0U);
    if (block.terminator.has_value()) {
        WriteTerminator(out, *block.terminator);
    }
}

BasicBlock ReadBasicBlock(std::istream &in) {
    BasicBlock out;
    out.id = ReadU32(in);
    out.label = ReadString(in);
    const std::uint64_t inst_count = ReadU64(in);
    if (inst_count >
        std::numeric_limits<std::size_t>::max()) {
        throw IRException("instruction count exceeds platform limits");
    }
    out.instructions.reserve(inst_count);
    for (std::uint64_t i = 0; i < inst_count; ++i) {
        out.instructions.push_back(ReadInstruction(in));
    }
    if (ReadU8(in) != 0) {
        out.terminator = ReadTerminator(in);
    }
    return out;
}

void WriteStringSlotMap(std::ostream &out,
                        const std::unordered_map<std::string, SlotId> &map) {
    WriteU64(out, map.size());
    for (const auto &[key, value] : map) {
        WriteString(out, key);
        WriteU32(out, value);
    }
}

std::unordered_map<std::string, SlotId> ReadStringSlotMap(std::istream &in) {
    const std::uint64_t count = ReadU64(in);
    if (count >
        std::numeric_limits<std::size_t>::max()) {
        throw IRException("map size exceeds platform limits");
    }
    std::unordered_map<std::string, SlotId> out;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        std::string key = ReadString(in);
        const SlotId value = ReadU32(in);
        out.emplace(std::move(key), value);
    }
    return out;
}

void WriteStringStringMap(
    std::ostream &out,
    const std::unordered_map<std::string, std::string> &map) {
    WriteU64(out, map.size());
    for (const auto &[key, value] : map) {
        WriteString(out, key);
        WriteString(out, value);
    }
}

std::unordered_map<std::string, std::string>
ReadStringStringMap(std::istream &in) {
    const std::uint64_t count = ReadU64(in);
    if (count >
        std::numeric_limits<std::size_t>::max()) {
        throw IRException("map size exceeds platform limits");
    }
    std::unordered_map<std::string, std::string> out;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        std::string key = ReadString(in);
        std::string value = ReadString(in);
        out.emplace(std::move(key), std::move(value));
    }
    return out;
}

void WriteFunction(std::ostream &out, const Function &function) {
    WriteString(out, function.name);
    WriteStringVector(out, function.params);
    WriteStringVector(out, function.param_types);
    WriteVectorScalar<SlotId>(out, function.param_slots);
    WriteStringVector(out, function.slot_names);
    WriteStringVector(out, function.slot_types);

    WriteU64(out, function.blocks.size());
    for (const BasicBlock &block : function.blocks) {
        WriteBasicBlock(out, block);
    }

    WriteU32(out, function.entry);
    WriteU32(out, function.next_temp);
    WriteU32(out, function.next_slot);
    WriteU8(out, function.is_method ? 1U : 0U);
    WriteString(out, function.owning_class);
    WriteString(out, function.method_name);
}

Function ReadFunction(std::istream &in) {
    Function function;
    function.name = ReadString(in);
    function.params = ReadStringVector(in);
    function.param_types = ReadStringVector(in);
    function.param_slots = ReadVectorScalar<SlotId>(in);
    function.slot_names = ReadStringVector(in);
    function.slot_types = ReadStringVector(in);

    const std::uint64_t block_count = ReadU64(in);
    if (block_count >
        std::numeric_limits<std::size_t>::max()) {
        throw IRException("block count exceeds platform limits");
    }
    function.blocks.reserve(block_count);
    for (std::uint64_t i = 0; i < block_count; ++i) {
        function.blocks.push_back(ReadBasicBlock(in));
    }

    function.entry = ReadU32(in);
    function.next_temp = ReadU32(in);
    function.next_slot = ReadU32(in);
    function.is_method = ReadU8(in) != 0;
    function.owning_class = ReadString(in);
    function.method_name = ReadString(in);
    return function;
}

void WriteClassInfo(std::ostream &out, const ClassInfo &class_info) {
    WriteString(out, class_info.name);
    WriteString(out, class_info.base_class);
    WriteStringVector(out, class_info.fields);
    WriteStringVector(out, class_info.field_types);
    WriteStringSlotMap(out, class_info.field_offsets);
    WriteStringSlotMap(out, class_info.method_slots);
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
    class_info.field_offsets = ReadStringSlotMap(in);
    class_info.method_slots = ReadStringSlotMap(in);
    class_info.vtable_functions = ReadStringVector(in);
    class_info.method_functions = ReadStringStringMap(in);
    class_info.constructor_function = ReadString(in);
    return class_info;
}

void WriteProgram(std::ostream &out, const Program &program) {
    WriteU64(out, program.functions.size());
    for (const Function &function : program.functions) {
        WriteFunction(out, function);
    }

    WriteU64(out, program.classes.size());
    for (const ClassInfo &class_info : program.classes) {
        WriteClassInfo(out, class_info);
    }

    WriteString(out, program.entry_function);
}

Program ReadProgram(std::istream &in) {
    Program program;
    const std::uint64_t function_count = ReadU64(in);
    if (function_count >
        std::numeric_limits<std::size_t>::max()) {
        throw IRException("function count exceeds platform limits");
    }
    program.functions.reserve(function_count);
    for (std::uint64_t i = 0; i < function_count; ++i) {
        program.functions.push_back(ReadFunction(in));
    }

    const std::uint64_t class_count = ReadU64(in);
    if (class_count >
        std::numeric_limits<std::size_t>::max()) {
        throw IRException("class count exceeds platform limits");
    }
    program.classes.reserve(class_count);
    for (std::uint64_t i = 0; i < class_count; ++i) {
        program.classes.push_back(ReadClassInfo(in));
    }

    program.entry_function = ReadString(in);
    return program;
}

void WriteProgramUnit(std::ostream &out, const ProgramUnit &unit) {
    WriteString(out, unit.name);
    WriteProgram(out, unit.program);
}

ProgramUnit ReadProgramUnit(std::istream &in) {
    ProgramUnit unit;
    unit.name = ReadString(in);
    unit.program = ReadProgram(in);
    return unit;
}

} // namespace

void WriteProgramBundleBinary(const ProgramBundle &bundle,
                              const std::filesystem::path &output_path) {
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw IRException("failed to open bundle output: " +
                          output_path.string());
    }

    WriteBytes(out, kBundleMagic.data(), kBundleMagic.size());

    WriteU64(out, bundle.prelude_units.size());
    for (const ProgramUnit &unit : bundle.prelude_units) {
        WriteProgramUnit(out, unit);
    }
    WriteProgram(out, bundle.program);
}

ProgramBundle ReadProgramBundleBinary(const std::filesystem::path &input_path) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        throw IRException("failed to open bundle input: " +
                          input_path.string());
    }

    std::array<char, 8> magic{};
    ReadBytes(in, magic.data(), magic.size());
    if (magic != kBundleMagic) {
        throw IRException("invalid Neon IR bundle magic");
    }

    ProgramBundle bundle;
    const std::uint64_t prelude_count = ReadU64(in);
    if (prelude_count >
        std::numeric_limits<std::size_t>::max()) {
        throw IRException("prelude unit count exceeds platform limits");
    }
    bundle.prelude_units.reserve(prelude_count);
    for (std::uint64_t i = 0; i < prelude_count; ++i) {
        bundle.prelude_units.push_back(ReadProgramUnit(in));
    }
    bundle.program = ReadProgram(in);
    return bundle;
}

} // namespace compiler::ir
