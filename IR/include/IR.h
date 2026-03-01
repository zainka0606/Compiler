#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace compiler::ir {

class IRException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

using LocalId = std::uint32_t;
using SlotId = std::uint32_t;
using BlockId = std::uint32_t;
constexpr LocalId kInvalidLocal = static_cast<LocalId>(-1);
constexpr SlotId kInvalidSlot = static_cast<SlotId>(-1);

enum class ValueKind {
    Temp,
    Number,
    String,
    Bool,
    Char,
    Null,
};

struct ValueRef {
    ValueKind kind = ValueKind::Null;
    LocalId temp = kInvalidLocal;
    double number = 0.0;
    std::string text;
    bool boolean = false;
    char character = '\0';

    static ValueRef Temp(LocalId id);
    static ValueRef Number(double value);
    static ValueRef String(std::string value);
    static ValueRef Bool(bool value);
    static ValueRef Char(char value);
    static ValueRef Null();

    [[nodiscard]] bool IsImmediate() const;
};

enum class UnaryOp {
    Negate,
    LogicalNot,
    BitwiseNot,
};

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    IntDivide,
    Modulo,
    Pow,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    ShiftLeft,
    ShiftRight,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

struct LoadLocalInst {
    LocalId dst = kInvalidLocal;
    SlotId slot = kInvalidSlot;
};

struct StoreLocalInst {
    SlotId slot = kInvalidSlot;
    ValueRef value;
};

struct DeclareGlobalInst {
    std::string name;
    std::string type_name;
    ValueRef value;
};

struct LoadGlobalInst {
    LocalId dst = kInvalidLocal;
    std::string name;
};

struct StoreGlobalInst {
    std::string name;
    ValueRef value;
};

struct MoveInst {
    LocalId dst = kInvalidLocal;
    ValueRef src;
};

struct UnaryInst {
    LocalId dst = kInvalidLocal;
    UnaryOp op = UnaryOp::Negate;
    ValueRef value;
};

struct BinaryInst {
    LocalId dst = kInvalidLocal;
    BinaryOp op = BinaryOp::Add;
    ValueRef lhs;
    ValueRef rhs;
};

struct MakeArrayInst {
    LocalId dst = kInvalidLocal;
    std::vector<ValueRef> elements;
};

struct ArrayLoadInst {
    LocalId dst = kInvalidLocal;
    ValueRef array;
    ValueRef index;
};

struct ArrayStoreInst {
    ValueRef array;
    ValueRef index;
    ValueRef value;
};

struct ResolveFieldOffsetInst {
    LocalId dst = kInvalidLocal;
    ValueRef object;
    std::string member;
};

struct ObjectLoadInst {
    LocalId dst = kInvalidLocal;
    ValueRef object;
    ValueRef offset;
};

struct ObjectStoreInst {
    ValueRef object;
    ValueRef offset;
    ValueRef value;
};

struct ResolveMethodSlotInst {
    LocalId dst = kInvalidLocal;
    ValueRef object;
    std::string method;
};

struct CallInst {
    LocalId dst = kInvalidLocal;
    std::string callee;
    std::vector<ValueRef> args;
};

struct VirtualCallInst {
    LocalId dst = kInvalidLocal;
    ValueRef object;
    ValueRef slot;
    std::vector<ValueRef> args;
};

using Instruction =
    std::variant<LoadLocalInst, StoreLocalInst, DeclareGlobalInst,
                 LoadGlobalInst, StoreGlobalInst, MoveInst, UnaryInst,
                 BinaryInst, MakeArrayInst, ArrayLoadInst, ArrayStoreInst,
                 ResolveFieldOffsetInst, ObjectLoadInst, ObjectStoreInst,
                 ResolveMethodSlotInst, CallInst, VirtualCallInst>;

struct JumpTerm {
    BlockId target = 0;
};

struct BranchTerm {
    ValueRef condition;
    BlockId true_target = 0;
    BlockId false_target = 0;
};

struct ReturnTerm {
    std::optional<ValueRef> value;
};

using Terminator = std::variant<JumpTerm, BranchTerm, ReturnTerm>;

struct BasicBlock {
    BlockId id = 0;
    std::string label;
    std::vector<Instruction> instructions;
    std::optional<Terminator> terminator;
};

struct Function {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> param_types;
    std::vector<SlotId> param_slots;
    std::vector<std::string> slot_names;
    std::vector<std::string> slot_types;
    std::vector<BasicBlock> blocks;
    BlockId entry = 0;
    LocalId next_temp = 0;
    SlotId next_slot = 0;
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

struct Program {
    std::vector<Function> functions;
    std::vector<ClassInfo> classes;
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

struct OptimizationOptions {
    std::vector<std::string> disabled_passes;
    int max_rounds = 16;
};

class FunctionBuilder {
  public:
    explicit FunctionBuilder(Function &function);

    BlockId CreateBlock(std::string label);
    void SetCurrent(BlockId id);
    [[nodiscard]] BlockId Current() const;

    [[nodiscard]] LocalId NewTemp();
    [[nodiscard]] SlotId NewSlot(std::string name);

    void Emit(Instruction instruction);
    void SetTerminator(Terminator terminator);

    [[nodiscard]] bool HasTerminator(BlockId id) const;
    [[nodiscard]] BasicBlock &CurrentBlock();
    [[nodiscard]] const BasicBlock &CurrentBlock() const;

    [[nodiscard]] ValueRef Temp(LocalId id) const;

  private:
    Function *function_ = nullptr;
    BlockId current_ = 0;
};

struct ObjectInstance;
struct ArrayInstance;
using ObjectInstancePtr = std::shared_ptr<ObjectInstance>;
using ArrayInstancePtr = std::shared_ptr<ArrayInstance>;
using Value = std::variant<std::monostate, double, std::string, bool, char,
                           ObjectInstancePtr, ArrayInstancePtr>;

struct ObjectInstance {
    std::string class_name;
    const ClassInfo *class_layout = nullptr;
    std::vector<Value> memory;
    std::vector<const Function *> vtable;
};

struct ArrayInstance {
    std::vector<Value> elements;
};

std::string ValueToString(const Value &value);

std::string ProgramToAssembly(const Program &program,
                              std::string_view unit_name = "program");

std::vector<std::string> ListOptimizationPasses();

void OptimizeProgram(Program &program, const OptimizationOptions &options);

void OptimizeProgram(Program &program);

void WriteProgramBundleBinary(const ProgramBundle &bundle,
                              const std::filesystem::path &output_path);

ProgramBundle ReadProgramBundleBinary(const std::filesystem::path &input_path);

Value ExecuteProgram(const Program &program,
                     const std::vector<ProgramUnit> &prelude_units = {});

} // namespace compiler::ir
