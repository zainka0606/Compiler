#pragma once

#include "IR.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace compiler::bytecode {

class BytecodeException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

using RegisterId = std::uint32_t;
using SlotId = std::uint32_t;
using Address = std::uint32_t;

constexpr RegisterId kInvalidRegister = static_cast<RegisterId>(-1);
constexpr SlotId kInvalidSlot = static_cast<SlotId>(-1);
constexpr Address kInvalidAddress = static_cast<Address>(-1);
constexpr Address kBuiltinAddressBase = 0x80000000U;
constexpr RegisterId kRegisterCount = 16;
constexpr RegisterId kStackPointerRegister = 16;
constexpr RegisterId kBasePointerRegister = 17;
constexpr RegisterId kAllocatableRegisterCount = 16;
constexpr RegisterId kSelfRegister = 13;
constexpr RegisterId kArgRegisterCount = 8;

enum class OperandKind : std::uint8_t {
    Register = 0,
    StackSlot = 1,
    Number = 2,
    String = 3,
    Bool = 4,
    Char = 5,
    Null = 6,
};

struct Operand {
    OperandKind kind = OperandKind::Null;
    RegisterId reg = kInvalidRegister;
    SlotId stack_slot = kInvalidSlot;
    double number = 0.0;
    std::string text;
    bool boolean = false;
    char character = '\0';

    static Operand Register(RegisterId value);
    static Operand StackSlot(SlotId value);
    static Operand Number(double value);
    static Operand String(std::string value);
    static Operand Bool(bool value);
    static Operand Char(char value);
    static Operand Null();
};

enum class OpCode : std::uint8_t {
    Nop = 0,
    Load = 1,
    Store = 2,
    Push = 3,
    Pop = 4,
    DeclareGlobal = 5,
    Move = 6,
    Unary = 7,
    Binary = 8,
    Compare = 9,
    MakeArray = 10,
    StackAllocObject = 11,
    Call = 12,
    CallRegister = 13,
    Jump = 14,
    JumpIfFalse = 15,
    JumpCarry = 16,
    JumpNotCarry = 17,
    JumpZero = 18,
    JumpNotZero = 19,
    JumpSign = 20,
    JumpNotSign = 21,
    JumpOverflow = 22,
    JumpNotOverflow = 23,
    Return = 24,
    JumpAbove = 25,
    JumpAboveEqual = 26,
    JumpBelow = 27,
    JumpBelowEqual = 28,
    JumpGreater = 29,
    JumpGreaterEqual = 30,
    JumpLess = 31,
    JumpLessEqual = 32,
};

enum class LoadMode : std::uint8_t {
    StackRelative = 0,
    StackAbsolute = 1,
    Global = 2,
    ArrayElement = 3,
    ObjectOffset = 4,
    FieldOffsetByName = 5,
    MethodSlotByName = 6,
    MethodFunctionBySlot = 7,
};

enum class StoreMode : std::uint8_t {
    StackRelative = 0,
    StackAbsolute = 1,
    Global = 2,
    ArrayElement = 3,
    ObjectOffset = 4,
};

struct Instruction {
    OpCode opcode = OpCode::Nop;
    LoadMode load_mode = LoadMode::StackRelative;
    StoreMode store_mode = StoreMode::StackRelative;
    RegisterId dst = kInvalidRegister;
    SlotId dst_slot = kInvalidSlot;
    SlotId slot = kInvalidSlot;
    Operand a;
    Operand b;
    Operand c;
    std::vector<Operand> operands;
    std::string text;
    std::string text2;
    compiler::ir::UnaryOp unary_op = compiler::ir::UnaryOp::Negate;
    compiler::ir::BinaryOp binary_op = compiler::ir::BinaryOp::Add;
    Address target = 0;
};

struct Function {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> param_types;
    std::vector<SlotId> param_slots;
    std::vector<std::string> slot_names;
    std::vector<std::string> slot_types;
    std::vector<Instruction> code;
    Address entry_pc = 0;
    RegisterId register_count = kRegisterCount;
    SlotId local_count = 0;
    SlotId stack_slot_count = 0;
    bool is_method = false;
    std::string owning_class;
    std::string method_name;
};

struct ClassInfo {
    std::string name;
    std::string base_class;
    std::vector<std::string> fields;
    std::vector<std::string> field_types;
    std::unordered_map<std::string, SlotId> field_offsets;
    std::unordered_map<std::string, SlotId> method_slots;
    std::vector<std::string> vtable_functions;
    std::unordered_map<std::string, std::string> method_functions;
    std::string constructor_function;
};

struct GlobalVariable {
    std::string name;
    std::string type_name;
    Operand value;
};

struct Program {
    std::vector<Function> functions;
    std::vector<ClassInfo> classes;
    std::vector<GlobalVariable> globals;
    std::string entry_function = "__main__";
};

struct ProgramUnit {
    std::string name;
    Program program;
};

struct ProgramBundle {
    Program program;
    std::vector<ProgramUnit> prelude_units;
};

Program LowerIRProgram(const compiler::ir::Program &program);
ProgramBundle LowerIRBundle(const compiler::ir::ProgramBundle &bundle);

struct OptimizationOptions {
    std::vector<std::string> disabled_passes;
    int max_rounds = 4;
};

std::vector<std::string> ListOptimizationPasses();
void OptimizeProgram(Program &program, const OptimizationOptions &options);
void OptimizeProgram(Program &program);

Address BuiltinAddressForName(std::string_view name);
std::string_view BuiltinNameForAddress(Address address);

std::string ProgramToAssembly(const Program &program,
                              std::string_view unit_name = "program");
std::string ProgramBundleToAssembly(const ProgramBundle &bundle,
                                    std::string_view main_unit_name = "program");

void WriteProgramBundleBinary(const ProgramBundle &bundle,
                              const std::filesystem::path &output_path);

ProgramBundle ReadProgramBundleBinary(const std::filesystem::path &input_path);

} // namespace compiler::bytecode
