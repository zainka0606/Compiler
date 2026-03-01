#include "IR.h"
#include "Common/NumberParsing.h"
#include "passes/PassFactories.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace compiler::ir {

ValueRef ValueRef::Temp(const LocalId id) {
    ValueRef out;
    out.kind = ValueKind::Temp;
    out.temp = id;
    return out;
}

ValueRef ValueRef::Number(const double value) {
    ValueRef out;
    out.kind = ValueKind::Number;
    out.number = value;
    return out;
}

ValueRef ValueRef::String(std::string value) {
    ValueRef out;
    out.kind = ValueKind::String;
    out.text = std::move(value);
    return out;
}

ValueRef ValueRef::Bool(const bool value) {
    ValueRef out;
    out.kind = ValueKind::Bool;
    out.boolean = value;
    return out;
}

ValueRef ValueRef::Char(const char value) {
    ValueRef out;
    out.kind = ValueKind::Char;
    out.character = value;
    return out;
}

ValueRef ValueRef::Null() {
    ValueRef out;
    out.kind = ValueKind::Null;
    return out;
}

bool ValueRef::IsImmediate() const { return kind != ValueKind::Temp; }

FunctionBuilder::FunctionBuilder(Function &function)
    : function_(&function), current_(function.entry) {
    if (function_->blocks.empty()) {
        function_->blocks.push_back(BasicBlock{.id = 0, .label = "entry"});
    }
    current_ = function_->entry;
}

BlockId FunctionBuilder::CreateBlock(std::string label) {
    const BlockId id = static_cast<BlockId>(function_->blocks.size());
    function_->blocks.push_back(
        BasicBlock{.id = id, .label = std::move(label)});
    return id;
}

void FunctionBuilder::SetCurrent(const BlockId id) {
    if (id >= function_->blocks.size()) {
        throw IRException("invalid block id in FunctionBuilder::SetCurrent");
    }
    current_ = id;
}

BlockId FunctionBuilder::Current() const { return current_; }

LocalId FunctionBuilder::NewTemp() { return function_->next_temp++; }

SlotId FunctionBuilder::NewSlot(std::string name) {
    const SlotId slot = function_->next_slot++;
    function_->slot_names.push_back(std::move(name));
    function_->slot_types.emplace_back();
    return slot;
}

void FunctionBuilder::Emit(Instruction instruction) {
    BasicBlock &block = CurrentBlock();
    if (block.terminator.has_value()) {
        throw IRException("cannot emit instruction into terminated block");
    }
    block.instructions.push_back(std::move(instruction));
}

void FunctionBuilder::SetTerminator(Terminator terminator) {
    CurrentBlock().terminator = std::move(terminator);
}

bool FunctionBuilder::HasTerminator(const BlockId id) const {
    if (id >= function_->blocks.size()) {
        throw IRException("invalid block id in FunctionBuilder::HasTerminator");
    }
    return function_->blocks[id].terminator.has_value();
}

BasicBlock &FunctionBuilder::CurrentBlock() {
    return function_->blocks[current_];
}

const BasicBlock &FunctionBuilder::CurrentBlock() const {
    return function_->blocks[current_];
}

ValueRef FunctionBuilder::Temp(const LocalId id) const { return ValueRef::Temp(id); }

namespace {

std::string NumberToString(const double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

std::string UnsignedToHex(const std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

std::string NumberToAssemblyLiteral(const double value) {
    if (std::isfinite(value)) {
        const double rounded = std::round(value);
        const double tolerance = std::numeric_limits<double>::epsilon() * 64.0 *
                                 std::max(1.0, std::abs(value));
        if (std::abs(value - rounded) <= tolerance) {
            if (rounded >= 16.0 &&
                rounded <= static_cast<double>(
                               std::numeric_limits<std::uint64_t>::max())) {
                return "0x" +
                       UnsignedToHex(static_cast<std::uint64_t>(rounded));
            }
            if (rounded <= -16.0 &&
                -rounded <= static_cast<double>(
                                std::numeric_limits<std::uint64_t>::max())) {
                return "-0x" +
                       UnsignedToHex(static_cast<std::uint64_t>(-rounded));
            }
        }
    }
    return NumberToString(value);
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
        case '"':
            out += "\\\"";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string ValueRefToAssembly(const ValueRef &value) {
    switch (value.kind) {
    case ValueKind::Temp:
        return "%" + std::to_string(value.temp);
    case ValueKind::Number:
        return NumberToAssemblyLiteral(value.number);
    case ValueKind::String:
        return "\"" + EscapeString(value.text) + "\"";
    case ValueKind::Bool:
        return value.boolean ? "true" : "false";
    case ValueKind::Char:
        return std::string("'") + value.character + "'";
    case ValueKind::Null:
        return "null";
    }
    return "null";
}

const char *UnaryOpName(const UnaryOp op) {
    switch (op) {
    case UnaryOp::Negate:
        return "neg";
    case UnaryOp::LogicalNot:
        return "lnot";
    case UnaryOp::BitwiseNot:
        return "bnot";
    }
    return "?";
}

const char *BinaryOpName(const BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:
        return "add";
    case BinaryOp::Subtract:
        return "sub";
    case BinaryOp::Multiply:
        return "mul";
    case BinaryOp::Divide:
        return "div";
    case BinaryOp::IntDivide:
        return "idiv";
    case BinaryOp::Modulo:
        return "mod";
    case BinaryOp::Pow:
        return "pow";
    case BinaryOp::BitwiseAnd:
        return "band";
    case BinaryOp::BitwiseOr:
        return "bor";
    case BinaryOp::BitwiseXor:
        return "bxor";
    case BinaryOp::ShiftLeft:
        return "shl";
    case BinaryOp::ShiftRight:
        return "shr";
    case BinaryOp::Equal:
        return "eq";
    case BinaryOp::NotEqual:
        return "neq";
    case BinaryOp::Less:
        return "lt";
    case BinaryOp::LessEqual:
        return "le";
    case BinaryOp::Greater:
        return "gt";
    case BinaryOp::GreaterEqual:
        return "ge";
    }
    return "?";
}

std::string InstructionToAssembly(const Instruction &instruction) {
    return std::visit(
        [](const auto &inst) -> std::string {
            using T = std::decay_t<decltype(inst)>;
            if constexpr (std::is_same_v<T, LoadLocalInst>) {
                return "%" + std::to_string(inst.dst) + " = ldloc s" +
                       std::to_string(inst.slot);
            } else if constexpr (std::is_same_v<T, StoreLocalInst>) {
                return "stloc s" + std::to_string(inst.slot) + ", " +
                       ValueRefToAssembly(inst.value);
            } else if constexpr (std::is_same_v<T, DeclareGlobalInst>) {
                std::string out = "defg @" + inst.name;
                if (!inst.type_name.empty()) {
                    out += ":" + inst.type_name;
                }
                out += ", " + ValueRefToAssembly(inst.value);
                return out;
            } else if constexpr (std::is_same_v<T, LoadGlobalInst>) {
                return "%" + std::to_string(inst.dst) + " = ldg @" + inst.name;
            } else if constexpr (std::is_same_v<T, StoreGlobalInst>) {
                return "stg @" + inst.name + ", " +
                       ValueRefToAssembly(inst.value);
            } else if constexpr (std::is_same_v<T, MoveInst>) {
                return "%" + std::to_string(inst.dst) + " = mov " +
                       ValueRefToAssembly(inst.src);
            } else if constexpr (std::is_same_v<T, UnaryInst>) {
                return "%" + std::to_string(inst.dst) + " = " +
                       UnaryOpName(inst.op) + " " +
                       ValueRefToAssembly(inst.value);
            } else if constexpr (std::is_same_v<T, BinaryInst>) {
                return "%" + std::to_string(inst.dst) + " = " +
                       BinaryOpName(inst.op) + " " +
                       ValueRefToAssembly(inst.lhs) + ", " +
                       ValueRefToAssembly(inst.rhs);
            } else if constexpr (std::is_same_v<T, MakeArrayInst>) {
                std::ostringstream out;
                out << "%" << inst.dst << " = array [";
                for (std::size_t i = 0; i < inst.elements.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << ValueRefToAssembly(inst.elements[i]);
                }
                out << "]";
                return out.str();
            } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                return "%" + std::to_string(inst.dst) + " = aload " +
                       ValueRefToAssembly(inst.array) + ", " +
                       ValueRefToAssembly(inst.index);
            } else if constexpr (std::is_same_v<T, ArrayStoreInst>) {
                return "astore " + ValueRefToAssembly(inst.array) + ", " +
                       ValueRefToAssembly(inst.index) + ", " +
                       ValueRefToAssembly(inst.value);
            } else if constexpr (std::is_same_v<T, ResolveFieldOffsetInst>) {
                return "%" + std::to_string(inst.dst) + " = fldoff " +
                       ValueRefToAssembly(inst.object) + ", ." + inst.member;
            } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                return "%" + std::to_string(inst.dst) + " = loado " +
                       ValueRefToAssembly(inst.object) + ", " +
                       ValueRefToAssembly(inst.offset);
            } else if constexpr (std::is_same_v<T, ObjectStoreInst>) {
                return "storeo " + ValueRefToAssembly(inst.object) + ", " +
                       ValueRefToAssembly(inst.offset) + ", " +
                       ValueRefToAssembly(inst.value);
            } else if constexpr (std::is_same_v<T, ResolveMethodSlotInst>) {
                return "%" + std::to_string(inst.dst) + " = mslot " +
                       ValueRefToAssembly(inst.object) + ", ." + inst.method;
            } else if constexpr (std::is_same_v<T, CallInst>) {
                std::ostringstream out;
                out << "%" << inst.dst << " = call " << inst.callee << "(";
                for (std::size_t i = 0; i < inst.args.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << ValueRefToAssembly(inst.args[i]);
                }
                out << ")";
                return out.str();
            } else if constexpr (std::is_same_v<T, VirtualCallInst>) {
                std::ostringstream out;
                out << "%" << inst.dst << " = vcall "
                    << ValueRefToAssembly(inst.object) << ", "
                    << ValueRefToAssembly(inst.slot) << "(";
                for (std::size_t i = 0; i < inst.args.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << ValueRefToAssembly(inst.args[i]);
                }
                out << ")";
                return out.str();
            }
            return "<unknown>";
        },
        instruction);
}

std::string TerminatorToAssembly(const Terminator &terminator) {
    return std::visit(
        [](const auto &term) -> std::string {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, JumpTerm>) {
                return "jmp b" + std::to_string(term.target);
            } else if constexpr (std::is_same_v<T, BranchTerm>) {
                return "br " + ValueRefToAssembly(term.condition) + ", b" +
                       std::to_string(term.true_target) + ", b" +
                       std::to_string(term.false_target);
            } else if constexpr (std::is_same_v<T, ReturnTerm>) {
                if (term.value.has_value()) {
                    return "ret " + ValueRefToAssembly(*term.value);
                }
                return "ret";
            }
            return "ret";
        },
        terminator);
}

struct RuntimeEnvironment {
    struct GlobalBinding {
        Value value = std::monostate{};
        std::string type_name;
    };

    std::unordered_map<std::string, const Function *> functions;
    std::unordered_map<std::string, const ClassInfo *> classes;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, const Function *>>
        class_methods;
    std::unordered_map<std::string, const Function *> class_constructors;
    std::unordered_map<std::string, std::unordered_map<std::string, SlotId>>
        class_method_slots;
    std::unordered_map<std::string, std::vector<const Function *>>
        class_vtables;
    std::unordered_map<std::string, GlobalBinding> globals;
};

std::string ValueToStringInternal(const Value &value);

std::string ObjectToString(const ObjectInstancePtr &object) {
    if (!object) {
        return "null-object";
    }

    std::ostringstream out;
    out << object->class_name << "{";
    const ClassInfo *layout = object->class_layout;
    if (layout == nullptr) {
        out << "<no-layout>";
        out << "}";
        return out.str();
    }
    for (std::size_t i = 0; i < layout->fields.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << layout->fields[i] << "=";
        SlotId offset = static_cast<SlotId>(i);
        const auto offset_it = layout->field_offsets.find(layout->fields[i]);
        if (offset_it != layout->field_offsets.end()) {
            offset = offset_it->second;
        }
        if (offset < object->memory.size()) {
            out << ValueToStringInternal(object->memory[offset]);
        } else {
            out << "<undef>";
        }
    }
    out << "}";
    return out.str();
}

std::string ArrayToString(const ArrayInstancePtr &array) {
    if (!array) {
        return "null-array";
    }

    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < array->elements.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << ValueToStringInternal(array->elements[i]);
    }
    out << "]";
    return out.str();
}

std::string ValueToStringInternal(const Value &value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }
    if (const auto *number = std::get_if<double>(&value)) {
        return NumberToString(*number);
    }
    if (const auto *text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto *boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto *character = std::get_if<char>(&value)) {
        return std::string(1, *character);
    }
    if (const auto *object = std::get_if<ObjectInstancePtr>(&value)) {
        return ObjectToString(*object);
    }
    if (const auto *array = std::get_if<ArrayInstancePtr>(&value)) {
        return ArrayToString(*array);
    }
    return "null";
}

bool IsNumericLike(const Value &value) {
    return std::holds_alternative<double>(value) ||
           std::holds_alternative<bool>(value) ||
           std::holds_alternative<char>(value);
}

double ParseNumberFromText(const std::string_view text,
                           const std::string_view context) {
    try {
        return common::ParseNumericLiteral(text, context);
    } catch (const std::exception &ex) {
        throw IRException(ex.what());
    }
}

double AsNumber(const Value &value, const std::string_view context) {
    if (const auto *number = std::get_if<double>(&value)) {
        return *number;
    }
    if (const auto *boolean = std::get_if<bool>(&value)) {
        return *boolean ? 1.0 : 0.0;
    }
    if (const auto *character = std::get_if<char>(&value)) {
        return static_cast<unsigned char>(*character);
    }
    if (const auto *text = std::get_if<std::string>(&value)) {
        return ParseNumberFromText(*text, context);
    }
    if (std::holds_alternative<ObjectInstancePtr>(value)) {
        throw IRException(std::string("cannot use object as number in ") +
                          std::string(context));
    }
    if (std::holds_alternative<ArrayInstancePtr>(value)) {
        throw IRException(std::string("cannot use array as number in ") +
                          std::string(context));
    }
    throw IRException(std::string("cannot use null value as number in ") +
                      std::string(context));
}

std::int64_t AsInt64(const Value &value, const std::string_view context) {
    const double number = AsNumber(value, context);
    if (!std::isfinite(number)) {
        throw IRException("integer in " + std::string(context) +
                          " must be finite");
    }
    const double rounded = std::round(number);
    const double tolerance = std::numeric_limits<double>::epsilon() * 64.0 *
                             std::max(1.0, std::abs(number));
    if (std::abs(number - rounded) > tolerance) {
        throw IRException("integer in " + std::string(context) +
                          " must not have a fractional part (got " +
                          NumberToString(number) + ")");
    }
    if (rounded <
            static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        rounded >
            static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        throw IRException("integer in " + std::string(context) +
                          " is out of int64 range");
    }
    return static_cast<std::int64_t>(rounded);
}

bool IsTruthy(const Value &value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return false;
    }
    if (const auto *number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    if (const auto *text = std::get_if<std::string>(&value)) {
        return !text->empty();
    }
    if (const auto *boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }
    if (const auto *character = std::get_if<char>(&value)) {
        return *character != '\0';
    }
    if (const auto *object = std::get_if<ObjectInstancePtr>(&value)) {
        return *object != nullptr;
    }
    if (const auto *array = std::get_if<ArrayInstancePtr>(&value)) {
        return *array != nullptr && !(*array)->elements.empty();
    }
    return false;
}

bool ValueEquals(const Value &lhs, const Value &rhs) {
    if (lhs.index() == rhs.index()) {
        if (std::holds_alternative<std::monostate>(lhs)) {
            return true;
        }
        if (const auto *left = std::get_if<double>(&lhs)) {
            return *left == std::get<double>(rhs);
        }
        if (const auto *left = std::get_if<std::string>(&lhs)) {
            return *left == std::get<std::string>(rhs);
        }
        if (const auto *left = std::get_if<bool>(&lhs)) {
            return *left == std::get<bool>(rhs);
        }
        if (const auto *left = std::get_if<char>(&lhs)) {
            return *left == std::get<char>(rhs);
        }
        if (const auto *left = std::get_if<ObjectInstancePtr>(&lhs)) {
            return left->get() == std::get<ObjectInstancePtr>(rhs).get();
        }
        if (const auto *left = std::get_if<ArrayInstancePtr>(&lhs)) {
            return left->get() == std::get<ArrayInstancePtr>(rhs).get();
        }
    }

    if (IsNumericLike(lhs) && IsNumericLike(rhs)) {
        return AsNumber(lhs, "==") == AsNumber(rhs, "==");
    }

    return ValueToStringInternal(lhs) == ValueToStringInternal(rhs);
}

bool ValueLess(const Value &lhs, const Value &rhs) {
    if (IsNumericLike(lhs) && IsNumericLike(rhs)) {
        return AsNumber(lhs, "<") < AsNumber(rhs, "<");
    }
    return ValueToStringInternal(lhs) < ValueToStringInternal(rhs);
}

std::string AsString(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto *character = std::get_if<char>(&value)) {
        return std::string(1, *character);
    }
    return ValueToStringInternal(value);
}

std::size_t AsIndex(const Value &value, const std::string_view context) {
    const double raw = AsNumber(value, context);
    if (!std::isfinite(raw)) {
        throw IRException("index in " + std::string(context) +
                          " must be finite");
    }
    if (raw < 0.0) {
        throw IRException("index in " + std::string(context) +
                          " must be non-negative");
    }
    const double truncated = std::floor(raw);
    if (truncated != raw) {
        throw IRException("index in " + std::string(context) +
                          " must be an integer");
    }
    return static_cast<std::size_t>(truncated);
}

bool IsIntegralNumber(const double number) {
    if (!std::isfinite(number)) {
        return false;
    }
    const double rounded = std::round(number);
    const double tolerance = std::numeric_limits<double>::epsilon() * 64.0 *
                             std::max(1.0, std::abs(number));
    if (std::abs(number - rounded) > tolerance) {
        return false;
    }
    return rounded >=
               static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
           rounded <=
               static_cast<double>(std::numeric_limits<std::int64_t>::max());
}

std::string RuntimeTypeName(const Value &value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }
    if (std::holds_alternative<double>(value)) {
        return "number";
    }
    if (std::holds_alternative<std::string>(value)) {
        return "string";
    }
    if (std::holds_alternative<bool>(value)) {
        return "bool";
    }
    if (std::holds_alternative<char>(value)) {
        return "char";
    }
    if (const auto *object = std::get_if<ObjectInstancePtr>(&value)) {
        if (!*object) {
            return "null";
        }
        return (*object)->class_name;
    }
    if (const auto *array = std::get_if<ArrayInstancePtr>(&value)) {
        if (!*array) {
            return "null";
        }
        return "array";
    }
    return "unknown";
}

std::string TrimTypeName(const std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string LowerAscii(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool IsArrayTypeName(const std::string_view type_name) {
    return type_name.size() >= 2 &&
           type_name.substr(type_name.size() - 2) == "[]";
}

std::string ElementTypeName(const std::string_view array_type_name) {
    return std::string(array_type_name.substr(0, array_type_name.size() - 2));
}

bool IsBuiltinTypeName(const std::string_view type_name) {
    return type_name == "number" || type_name == "int" || type_name == "bool" ||
           type_name == "string" || type_name == "char" ||
           type_name == "array" || type_name == "object" ||
           type_name == "null" || type_name == "any";
}

std::string NormalizeTypeName(const std::string_view type_name) {
    const std::string trimmed = TrimTypeName(type_name);
    if (trimmed.empty()) {
        return {};
    }

    if (IsArrayTypeName(trimmed)) {
        const std::string base = NormalizeTypeName(ElementTypeName(trimmed));
        if (base.empty()) {
            return {};
        }
        return base + "[]";
    }

    const std::string lowered = LowerAscii(trimmed);
    if (lowered == "number" || lowered == "num" || lowered == "float" ||
        lowered == "double") {
        return "number";
    }
    if (lowered == "int" || lowered == "integer" || lowered == "i64") {
        return "int";
    }
    if (lowered == "bool" || lowered == "boolean") {
        return "bool";
    }
    if (lowered == "string" || lowered == "str") {
        return "string";
    }
    if (lowered == "char") {
        return "char";
    }
    if (lowered == "array") {
        return "array";
    }
    if (lowered == "object") {
        return "object";
    }
    if (lowered == "null") {
        return "null";
    }
    if (lowered == "any") {
        return "any";
    }
    return trimmed;
}

std::string ResolveTypeName(const std::string_view type_name,
                            const RuntimeEnvironment &env,
                            const std::string_view context) {
    const std::string normalized = NormalizeTypeName(type_name);
    if (normalized.empty()) {
        return {};
    }

    if (IsArrayTypeName(normalized)) {
        const std::string resolved_base =
            ResolveTypeName(ElementTypeName(normalized), env, context);
        return resolved_base + "[]";
    }

    if (IsBuiltinTypeName(normalized)) {
        return normalized;
    }

    if (env.classes.contains(normalized)) {
        return normalized;
    }

    throw IRException("unknown type '" + normalized + "' in " +
                      std::string(context));
}

bool IsClassSubtype(const std::string_view actual_type,
                    const std::string_view expected_type,
                    const RuntimeEnvironment &env) {
    if (actual_type.empty() || expected_type.empty()) {
        return false;
    }
    if (actual_type == expected_type) {
        return true;
    }

    std::unordered_set<std::string> visited;
    std::string current(actual_type);
    while (!current.empty()) {
        if (current == expected_type) {
            return true;
        }
        if (!visited.insert(current).second) {
            return false;
        }
        const auto class_it = env.classes.find(current);
        if (class_it == env.classes.end() || class_it->second == nullptr) {
            return false;
        }
        current = class_it->second->base_class;
    }
    return false;
}

Value ValidateValueForDeclaredType(const Value &value,
                                   const std::string_view type_name,
                                   const RuntimeEnvironment &env,
                                   const std::string_view context) {
    const std::string resolved = ResolveTypeName(type_name, env, context);
    if (resolved.empty() || resolved == "any") {
        return value;
    }

    const auto fail_mismatch = [&]() -> Value {
        throw IRException(std::string(context) + ": expected type '" +
                          resolved + "', got '" + RuntimeTypeName(value) + "'");
    };

    if (resolved == "number") {
        if (!std::holds_alternative<double>(value)) {
            return fail_mismatch();
        }
        return value;
    }
    if (resolved == "int") {
        const auto *number = std::get_if<double>(&value);
        if (number == nullptr) {
            return fail_mismatch();
        }
        if (!IsIntegralNumber(*number)) {
            throw IRException(
                std::string(context) +
                ": expected integral number for type 'int', got " +
                NumberToString(*number));
        }
        return value;
    }
    if (resolved == "bool") {
        if (!std::holds_alternative<bool>(value)) {
            return fail_mismatch();
        }
        return value;
    }
    if (resolved == "string") {
        if (!std::holds_alternative<std::string>(value)) {
            return fail_mismatch();
        }
        return value;
    }
    if (resolved == "char") {
        if (!std::holds_alternative<char>(value)) {
            return fail_mismatch();
        }
        return value;
    }
    if (resolved == "null") {
        if (!std::holds_alternative<std::monostate>(value)) {
            return fail_mismatch();
        }
        return value;
    }
    if (resolved == "array") {
        const auto *array = std::get_if<ArrayInstancePtr>(&value);
        if (array == nullptr || !*array) {
            return fail_mismatch();
        }
        return value;
    }
    if (resolved == "object") {
        const auto *object = std::get_if<ObjectInstancePtr>(&value);
        if (object == nullptr || !*object) {
            return fail_mismatch();
        }
        return value;
    }
    if (IsArrayTypeName(resolved)) {
        const auto *array = std::get_if<ArrayInstancePtr>(&value);
        if (array == nullptr || !*array) {
            return fail_mismatch();
        }
        const std::string element_type = ElementTypeName(resolved);
        for (std::size_t i = 0; i < (*array)->elements.size(); ++i) {
            (void)ValidateValueForDeclaredType(
                (*array)->elements[i], element_type, env,
                std::string(context) + "[" + std::to_string(i) + "]");
        }
        return value;
    }

    const auto *object = std::get_if<ObjectInstancePtr>(&value);
    if (object == nullptr || !*object ||
        !IsClassSubtype((*object)->class_name, resolved, env)) {
        return fail_mismatch();
    }
    return value;
}

Value ExplicitCastValue(const Value &value, const std::string_view target_type,
                        const RuntimeEnvironment &env,
                        const std::string_view context) {
    const std::string resolved = ResolveTypeName(target_type, env, context);
    if (resolved.empty() || resolved == "any") {
        return value;
    }

    if (resolved == "number") {
        if (std::holds_alternative<double>(value)) {
            return value;
        }
        if (std::holds_alternative<bool>(value) ||
            std::holds_alternative<char>(value) ||
            std::holds_alternative<std::string>(value)) {
            return AsNumber(value, context);
        }
        throw IRException(std::string(context) + ": cannot cast '" +
                          RuntimeTypeName(value) + "' to 'number'");
    }

    if (resolved == "int") {
        if (std::holds_alternative<double>(value)) {
            const double number = std::get<double>(value);
            if (!IsIntegralNumber(number)) {
                throw IRException(std::string(context) +
                                  ": cannot cast non-integral value '" +
                                  NumberToString(number) + "' to 'int'");
            }
            return static_cast<double>(
                static_cast<std::int64_t>(std::round(number)));
        }
        if (std::holds_alternative<bool>(value) ||
            std::holds_alternative<char>(value) ||
            std::holds_alternative<std::string>(value)) {
            const double number = AsNumber(value, context);
            if (!IsIntegralNumber(number)) {
                throw IRException(std::string(context) +
                                  ": cannot cast non-integral value '" +
                                  NumberToString(number) + "' to 'int'");
            }
            return static_cast<double>(
                static_cast<std::int64_t>(std::round(number)));
        }
        throw IRException(std::string(context) + ": cannot cast '" +
                          RuntimeTypeName(value) + "' to 'int'");
    }

    if (resolved == "bool") {
        return IsTruthy(value);
    }

    if (resolved == "string") {
        return ValueToStringInternal(value);
    }

    if (resolved == "char") {
        if (const auto *character = std::get_if<char>(&value)) {
            return *character;
        }
        if (const auto *text = std::get_if<std::string>(&value)) {
            if (text->size() != 1) {
                throw IRException(std::string(context) +
                                  ": cannot cast string of length " +
                                  std::to_string(text->size()) + " to 'char'");
            }
            return (*text)[0];
        }
        if (std::holds_alternative<double>(value) ||
            std::holds_alternative<bool>(value) ||
            std::holds_alternative<char>(value) ||
            std::holds_alternative<std::string>(value)) {
            const std::int64_t codepoint =
                AsInt64(AsNumber(value, context), context);
            if (codepoint < 0 || codepoint > 255) {
                throw IRException(std::string(context) +
                                  ": char cast codepoint out of range [0,255]");
            }
            return static_cast<char>(codepoint);
        }
        throw IRException(std::string(context) + ": cannot cast '" +
                          RuntimeTypeName(value) + "' to 'char'");
    }

    if (resolved == "null") {
        if (!std::holds_alternative<std::monostate>(value)) {
            throw IRException(std::string(context) + ": cannot cast '" +
                              RuntimeTypeName(value) + "' to 'null'");
        }
        return value;
    }

    if (resolved == "array") {
        const auto *array = std::get_if<ArrayInstancePtr>(&value);
        if (array == nullptr || !*array) {
            throw IRException(std::string(context) + ": cannot cast '" +
                              RuntimeTypeName(value) + "' to 'array'");
        }
        return value;
    }

    if (resolved == "object") {
        const auto *object = std::get_if<ObjectInstancePtr>(&value);
        if (object == nullptr || !*object) {
            throw IRException(std::string(context) + ": cannot cast '" +
                              RuntimeTypeName(value) + "' to 'object'");
        }
        return value;
    }

    if (IsArrayTypeName(resolved)) {
        const auto *array = std::get_if<ArrayInstancePtr>(&value);
        if (array == nullptr || !*array) {
            throw IRException(std::string(context) + ": cannot cast '" +
                              RuntimeTypeName(value) + "' to '" + resolved +
                              "'");
        }
        const std::string element_type = ElementTypeName(resolved);
        auto casted = std::make_shared<ArrayInstance>();
        casted->elements.reserve((*array)->elements.size());
        for (std::size_t i = 0; i < (*array)->elements.size(); ++i) {
            casted->elements.push_back(ExplicitCastValue(
                (*array)->elements[i], element_type, env,
                std::string(context) + "[" + std::to_string(i) + "]"));
        }
        return casted;
    }

    const auto *object = std::get_if<ObjectInstancePtr>(&value);
    if (object == nullptr || !*object ||
        !IsClassSubtype((*object)->class_name, resolved, env)) {
        throw IRException(std::string(context) + ": cannot cast '" +
                          RuntimeTypeName(value) + "' to '" + resolved + "'");
    }
    return value;
}

Value EvalValueRef(const ValueRef &value,
                   const std::unordered_map<LocalId, Value> &temps) {
    switch (value.kind) {
    case ValueKind::Temp: {
        const auto it = temps.find(value.temp);
        if (it == temps.end()) {
            throw IRException("read from undefined temp %" +
                              std::to_string(value.temp));
        }
        return it->second;
    }
    case ValueKind::Number:
        return value.number;
    case ValueKind::String:
        return value.text;
    case ValueKind::Bool:
        return value.boolean;
    case ValueKind::Char:
        return value.character;
    case ValueKind::Null:
        return std::monostate{};
    }
    return std::monostate{};
}

Value LookupGlobal(const std::string_view name, const RuntimeEnvironment &env) {
    const auto it = env.globals.find(std::string(name));
    if (it != env.globals.end()) {
        return it->second.value;
    }
    throw IRException("unknown identifier: " + std::string(name));
}

void DeclareGlobal(std::string name, std::string type_name, Value value,
                   RuntimeEnvironment &env) {
    const std::string context = "global declaration '" + name + "'";
    const std::string resolved_type =
        type_name.empty() ? std::string{}
                          : ResolveTypeName(type_name, env, context);
    env.globals[std::move(name)] = RuntimeEnvironment::GlobalBinding{
        .value =
            ValidateValueForDeclaredType(value, resolved_type, env, context),
        .type_name = resolved_type,
    };
}

void StoreGlobal(const std::string_view name, Value value, RuntimeEnvironment &env) {
    const auto it = env.globals.find(std::string(name));
    if (it == env.globals.end()) {
        throw IRException("assignment to unknown identifier: " +
                          std::string(name));
    }
    const std::string context =
        "assignment to global '" + std::string(name) + "'";
    it->second.value =
        ValidateValueForDeclaredType(value, it->second.type_name, env, context);
}

Value LoadLocal(const std::vector<Value> &locals, const SlotId slot,
                const std::string_view function_name) {
    if (slot >= locals.size()) {
        throw IRException("invalid local slot s" + std::to_string(slot) +
                          " in function '" + std::string(function_name) + "'");
    }
    return locals[slot];
}

void StoreLocal(std::vector<Value> &locals, const SlotId slot, Value value,
                const Function &function, RuntimeEnvironment &env) {
    if (slot >= locals.size()) {
        throw IRException("invalid local slot s" + std::to_string(slot) +
                          " in function '" + function.name + "'");
    }
    const std::string slot_name = slot < function.slot_names.size() && !function.slot_names[slot].empty()
                                      ? function.slot_names[slot]
                                      : "s" + std::to_string(slot);
    const std::string slot_type = slot < function.slot_types.size()
                                      ? function.slot_types[slot]
                                      : std::string{};
    const std::string context = "assignment to local '" + slot_name +
                                "' in function '" + function.name + "'";
    locals[slot] = ValidateValueForDeclaredType(value, slot_type, env, context);
}

void ValidateFunctionLayout(const Function &function) {
    if (function.param_slots.size() != function.params.size()) {
        throw IRException("function '" + function.name +
                          "' has mismatched param_slots and params");
    }
    if (function.param_types.size() != function.params.size()) {
        throw IRException("function '" + function.name +
                          "' has mismatched param_types and params");
    }
    if (function.next_slot != function.slot_names.size()) {
        throw IRException("function '" + function.name +
                          "' has mismatched next_slot and slot_names");
    }
    if (function.slot_types.size() != function.slot_names.size()) {
        throw IRException("function '" + function.name +
                          "' has mismatched slot_types and slot_names");
    }
    for (const SlotId slot : function.param_slots) {
        if (slot >= function.next_slot) {
            throw IRException("function '" + function.name +
                              "' has invalid parameter slot");
        }
    }
}

const Function *FindMethodOnClass(const RuntimeEnvironment &env,
                                  const std::string_view class_name,
                                  const std::string_view method_name) {
    const auto class_it = env.class_methods.find(std::string(class_name));
    if (class_it == env.class_methods.end()) {
        return nullptr;
    }
    const auto method_it = class_it->second.find(std::string(method_name));
    if (method_it == class_it->second.end()) {
        return nullptr;
    }
    return method_it->second;
}

const ClassInfo &RequireObjectLayout(const ObjectInstancePtr &object,
                                     const std::string_view context) {
    if (!object) {
        throw IRException(std::string(context) + ": null object");
    }
    if (object->class_layout == nullptr) {
        throw IRException(std::string(context) + ": object of type '" +
                          object->class_name + "' has no class layout");
    }
    return *object->class_layout;
}

SlotId ResolveObjectFieldOffset(const ObjectInstancePtr &object,
                                const std::string_view field_name,
                                const std::string_view context) {
    const ClassInfo &layout = RequireObjectLayout(object, context);
    const auto field_it = layout.field_offsets.find(std::string(field_name));
    if (field_it == layout.field_offsets.end()) {
        throw IRException(std::string(context) + ": unknown field '" +
                          std::string(field_name) + "' on type '" +
                          object->class_name + "'");
    }
    return field_it->second;
}

SlotId ResolveObjectMethodSlot(const ObjectInstancePtr &object,
                               const std::string_view method_name,
                               const std::string_view context) {
    const ClassInfo &layout = RequireObjectLayout(object, context);
    const auto method_it = layout.method_slots.find(std::string(method_name));
    if (method_it == layout.method_slots.end()) {
        throw IRException(std::string(context) + ": unknown method '" +
                          std::string(method_name) + "' on type '" +
                          object->class_name + "'");
    }
    return method_it->second;
}

Value ExecuteFunction(const Function &function, RuntimeEnvironment &env,
                      const std::vector<Value> &args,
                      const ObjectInstancePtr &self_object = nullptr);

ObjectInstancePtr CreateObjectInstance(const ClassInfo &class_info,
                                       RuntimeEnvironment &env) {
    auto object = std::make_shared<ObjectInstance>();
    object->class_name = class_info.name;
    object->class_layout = &class_info;
    object->memory.resize(class_info.fields.size() + 1U, std::monostate{});

    const auto vt_it = env.class_vtables.find(class_info.name);
    if (vt_it == env.class_vtables.end()) {
        throw IRException("missing vtable for class '" + class_info.name + "'");
    }
    object->vtable = vt_it->second;
    return object;
}

bool IsBuiltinFunction(const std::string_view name) {
    return name == "sin" || name == "cos" || name == "tan" || name == "sqrt" ||
           name == "abs" || name == "exp" || name == "ln" || name == "log10" ||
           name == "pow" || name == "min" || name == "max" || name == "sum" ||
           name == "print" || name == "println" || name == "readln" ||
           name == "input" || name == "read_file" || name == "write_file" ||
           name == "append_file" || name == "read_binary_file" ||
           name == "write_binary_file" || name == "append_binary_file" ||
           name == "file_exists" || name == "file_size" || name == "len" ||
           name == "push" || name == "pop" || name == "__cast" ||
           name == "__new" || name == "alloc" || name == "stack_alloc";
}

std::vector<std::uint8_t> ToByteBuffer(const Value &value,
                                       const std::string_view context) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return std::vector<std::uint8_t>(text->begin(), text->end());
    }
    if (const auto *character = std::get_if<char>(&value)) {
        return {static_cast<std::uint8_t>(*character)};
    }
    if (const auto *array = std::get_if<ArrayInstancePtr>(&value)) {
        if (!*array) {
            throw IRException(std::string(context) +
                              " expects a non-null array of bytes");
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve((*array)->elements.size());
        for (const Value &element : (*array)->elements) {
            const std::int64_t raw = AsInt64(element, context);
            if (raw < 0 || raw > 255) {
                throw IRException(std::string(context) +
                                  " byte value must be in range [0, 255]");
            }
            bytes.push_back(static_cast<std::uint8_t>(raw));
        }
        return bytes;
    }
    throw IRException(std::string(context) +
                      " expects a byte array, string, or char");
}

Value ByteArrayToValue(const std::vector<std::uint8_t> &bytes) {
    auto out = std::make_shared<ArrayInstance>();
    out->elements.reserve(bytes.size());
    for (const std::uint8_t byte : bytes) {
        out->elements.push_back(static_cast<double>(byte));
    }
    return out;
}

Value CallBuiltin(std::string_view name, const std::vector<Value> &args,
                  RuntimeEnvironment &env) {
    const auto require_count = [&](const std::size_t expected) {
        if (args.size() != expected) {
            throw IRException("function '" + std::string(name) + "' expects " +
                              std::to_string(expected) + " argument(s), got " +
                              std::to_string(args.size()));
        }
    };
    const auto require_min_count = [&](const std::size_t minimum) {
        if (args.size() < minimum) {
            throw IRException("function '" + std::string(name) +
                              "' expects at least " + std::to_string(minimum) +
                              " argument(s), got " +
                              std::to_string(args.size()));
        }
    };

    if (name == "__cast") {
        require_count(2);
        const auto *type_name = std::get_if<std::string>(&args[1]);
        if (type_name == nullptr) {
            throw IRException(
                "__cast expects a string type name as its second argument");
        }
        return ExplicitCastValue(args[0], *type_name, env, "__cast");
    }

    if (name == "__new") {
        require_min_count(1);
        const auto *type_name = std::get_if<std::string>(&args[0]);
        if (type_name == nullptr || type_name->empty()) {
            throw IRException("__new expects a non-empty class name string as "
                              "its first argument");
        }

        const auto class_it = env.classes.find(*type_name);
        if (class_it == env.classes.end()) {
            throw IRException("unknown type in __new: " + *type_name);
        }
        const ClassInfo &class_info = *class_it->second;
        ObjectInstancePtr object = CreateObjectInstance(class_info, env);

        std::vector<Value> ctor_args;
        ctor_args.reserve(args.size() > 1 ? args.size() - 1 : 0);
        for (std::size_t i = 1; i < args.size(); ++i) {
            ctor_args.push_back(args[i]);
        }

        const auto ctor_it = env.class_constructors.find(class_info.name);
        if (ctor_it != env.class_constructors.end()) {
            if (ctor_it->second == nullptr) {
                throw IRException("constructor resolution failed for type '" +
                                  class_info.name + "'");
            }
            (void)ExecuteFunction(*ctor_it->second, env, ctor_args, object);
            return object;
        }

        if (!ctor_args.empty()) {
            throw IRException(
                "type '" + class_info.name +
                "' has no constructor; new expects 0 argument(s), got " +
                std::to_string(ctor_args.size()));
        }

        return object;
    }

    if (name == "alloc") {
        require_count(1);
        const auto *type_name = std::get_if<std::string>(&args[0]);
        if (type_name == nullptr || type_name->empty()) {
            throw IRException("alloc expects a non-empty class name string as "
                              "its first argument");
        }
        const auto class_it = env.classes.find(*type_name);
        if (class_it == env.classes.end()) {
            throw IRException("unknown type in alloc: " + *type_name);
        }
        return CreateObjectInstance(*class_it->second, env);
    }

    if (name == "stack_alloc") {
        require_count(1);
        const auto *type_name = std::get_if<std::string>(&args[0]);
        if (type_name == nullptr || type_name->empty()) {
            throw IRException(
                "stack_alloc expects a non-empty class name string as its first argument");
        }
        const auto class_it = env.classes.find(*type_name);
        if (class_it == env.classes.end()) {
            throw IRException("unknown type in stack_alloc: " + *type_name);
        }
        return CreateObjectInstance(*class_it->second, env);
    }

    if (name == "print" || name == "println") {
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) {
                std::cout << ' ';
            }
            std::cout << ValueToStringInternal(args[i]);
        }
        if (name == "println") {
            std::cout << "\n";
        }
        std::cout.flush();
        if (args.empty()) {
            return std::monostate{};
        }
        return args.back();
    }

    if (name == "readln") {
        require_count(0);
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::string{};
        }
        return line;
    }

    if (name == "input") {
        if (args.size() > 1) {
            throw IRException(
                "function 'input' expects 0 or 1 argument(s), got " +
                std::to_string(args.size()));
        }
        if (!args.empty()) {
            std::cout << ValueToStringInternal(args[0]);
            std::cout.flush();
        }
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::string{};
        }
        return line;
    }

    if (name == "read_file") {
        require_count(1);
        const std::string path = AsString(args[0]);
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            throw IRException("read_file failed to open: " + path);
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    if (name == "write_file") {
        require_count(2);
        const std::string path = AsString(args[0]);
        const std::string content = AsString(args[1]);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw IRException("write_file failed to open: " + path);
        }
        output << content;
        if (!output.good()) {
            throw IRException("write_file failed to write: " + path);
        }
        return static_cast<double>(content.size());
    }

    if (name == "append_file") {
        require_count(2);
        const std::string path = AsString(args[0]);
        const std::string content = AsString(args[1]);
        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output.is_open()) {
            throw IRException("append_file failed to open: " + path);
        }
        output << content;
        if (!output.good()) {
            throw IRException("append_file failed to write: " + path);
        }
        return static_cast<double>(content.size());
    }

    if (name == "read_binary_file") {
        require_count(1);
        const std::string path = AsString(args[0]);
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            throw IRException("read_binary_file failed to open: " + path);
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        const std::string content = buffer.str();
        return ByteArrayToValue(
            std::vector<std::uint8_t>(content.begin(), content.end()));
    }

    if (name == "write_binary_file") {
        require_count(2);
        const std::string path = AsString(args[0]);
        const std::vector<std::uint8_t> bytes =
            ToByteBuffer(args[1], "write_binary_file");
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw IRException("write_binary_file failed to open: " + path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output.good()) {
            throw IRException("write_binary_file failed to write: " + path);
        }
        return static_cast<double>(bytes.size());
    }

    if (name == "append_binary_file") {
        require_count(2);
        const std::string path = AsString(args[0]);
        const std::vector<std::uint8_t> bytes =
            ToByteBuffer(args[1], "append_binary_file");
        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output.is_open()) {
            throw IRException("append_binary_file failed to open: " + path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output.good()) {
            throw IRException("append_binary_file failed to write: " + path);
        }
        return static_cast<double>(bytes.size());
    }

    if (name == "file_exists") {
        require_count(1);
        const std::string path = AsString(args[0]);
        return std::filesystem::exists(path);
    }

    if (name == "file_size") {
        require_count(1);
        const std::string path = AsString(args[0]);
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            throw IRException("file_size failed for '" + path +
                              "': " + error.message());
        }
        return static_cast<double>(size);
    }

    if (name == "len") {
        require_count(1);
        if (const auto *text = std::get_if<std::string>(&args[0])) {
            return static_cast<double>(text->size());
        }
        if (const auto *array = std::get_if<ArrayInstancePtr>(&args[0])) {
            if (!*array) {
                throw IRException("len expects a non-null array");
            }
            return static_cast<double>((*array)->elements.size());
        }
        throw IRException("len expects a string or array");
    }

    if (name == "push") {
        require_count(2);
        const auto *array = std::get_if<ArrayInstancePtr>(&args[0]);
        if (array == nullptr || !*array) {
            throw IRException("push expects an array as first argument");
        }
        (*array)->elements.push_back(args[1]);
        return static_cast<double>((*array)->elements.size());
    }

    if (name == "pop") {
        require_count(1);
        const auto *array = std::get_if<ArrayInstancePtr>(&args[0]);
        if (array == nullptr || !*array) {
            throw IRException("pop expects an array argument");
        }
        if ((*array)->elements.empty()) {
            throw IRException("pop on empty array");
        }
        Value out = (*array)->elements.back();
        (*array)->elements.pop_back();
        return out;
    }

    if (name == "sin") {
        require_count(1);
        return std::sin(AsNumber(args[0], "sin"));
    }
    if (name == "cos") {
        require_count(1);
        return std::cos(AsNumber(args[0], "cos"));
    }
    if (name == "tan") {
        require_count(1);
        return std::tan(AsNumber(args[0], "tan"));
    }
    if (name == "sqrt") {
        require_count(1);
        return std::sqrt(AsNumber(args[0], "sqrt"));
    }
    if (name == "abs") {
        require_count(1);
        return std::fabs(AsNumber(args[0], "abs"));
    }
    if (name == "exp") {
        require_count(1);
        return std::exp(AsNumber(args[0], "exp"));
    }
    if (name == "ln") {
        require_count(1);
        return std::log(AsNumber(args[0], "ln"));
    }
    if (name == "log10") {
        require_count(1);
        return std::log10(AsNumber(args[0], "log10"));
    }
    if (name == "pow") {
        require_count(2);
        return std::pow(AsNumber(args[0], "pow"), AsNumber(args[1], "pow"));
    }
    if (name == "min") {
        require_min_count(1);
        double out = AsNumber(args[0], "min");
        for (std::size_t i = 1; i < args.size(); ++i) {
            const double value = AsNumber(args[i], "min");
            if (value < out) {
                out = value;
            }
        }
        return out;
    }
    if (name == "max") {
        require_min_count(1);
        double out = AsNumber(args[0], "max");
        for (std::size_t i = 1; i < args.size(); ++i) {
            const double value = AsNumber(args[i], "max");
            if (value > out) {
                out = value;
            }
        }
        return out;
    }
    if (name == "sum") {
        require_min_count(1);
        double out = 0.0;
        for (const Value &value : args) {
            out += AsNumber(value, "sum");
        }
        return out;
    }

    throw IRException("unknown function: " + std::string(name));
}

Value ExecuteFunction(const Function &function, RuntimeEnvironment &env,
                      const std::vector<Value> &args,
                      const ObjectInstancePtr &self_object);

Value CallMethod(const ObjectInstancePtr &self_object, const Function &method,
                 const std::vector<Value> &args, RuntimeEnvironment &env) {
    if (!self_object) {
        throw IRException("cannot call method '" + method.name +
                          "' on null object");
    }
    if (method.params.size() != args.size()) {
        throw IRException("method '" + self_object->class_name + "." +
                          method.name + "' expects " +
                          std::to_string(method.params.size()) +
                          " argument(s), got " + std::to_string(args.size()));
    }
    return ExecuteFunction(method, env, args, self_object);
}

Value CallMethodByName(const ObjectInstancePtr &self_object,
                       const std::string_view method_name,
                       const std::vector<Value> &args,
                       RuntimeEnvironment &env) {
    if (!self_object) {
        throw IRException("cannot call method '" + std::string(method_name) +
                          "' on null object");
    }
    const Function *method =
        FindMethodOnClass(env, self_object->class_name, method_name);
    if (method == nullptr) {
        throw IRException("unknown method '" + std::string(method_name) +
                          "' on type '" + self_object->class_name + "'");
    }
    return CallMethod(self_object, *method, args, env);
}

bool TryCallOperator(const Value &target, const std::string_view method_name,
                     const std::vector<Value> &args, RuntimeEnvironment &env,
                     Value &out) {
    const auto *object = std::get_if<ObjectInstancePtr>(&target);
    if (object == nullptr || !*object) {
        return false;
    }
    const Function *method =
        FindMethodOnClass(env, (*object)->class_name, method_name);
    if (method == nullptr) {
        return false;
    }
    out = CallMethod(*object, *method, args, env);
    return true;
}

bool TryCallOperatorBool(const Value &target,
                         const std::string_view method_name,
                         const std::vector<Value> &args,
                         RuntimeEnvironment &env, bool &out) {
    Value method_result;
    if (!TryCallOperator(target, method_name, args, env, method_result)) {
        return false;
    }
    out = IsTruthy(method_result);
    return true;
}

Value ApplyUnary(const UnaryOp op, const Value &input, RuntimeEnvironment &env) {
    Value overloaded_result;
    switch (op) {
    case UnaryOp::Negate:
        if (TryCallOperator(input, "__neg__", {}, env, overloaded_result)) {
            return overloaded_result;
        }
        return -AsNumber(input, "unary -");
    case UnaryOp::LogicalNot:
        return !IsTruthy(input);
    case UnaryOp::BitwiseNot:
        return static_cast<double>(~AsInt64(input, "~"));
    }
    throw IRException("unsupported unary operation");
}

Value ApplyBinary(const BinaryOp op, const Value &lhs, const Value &rhs,
                  RuntimeEnvironment &env) {
    Value overloaded_result;
    switch (op) {
    case BinaryOp::Add:
        if (TryCallOperator(lhs, "__add__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__radd__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (std::holds_alternative<std::string>(lhs) ||
            std::holds_alternative<std::string>(rhs)) {
            return ValueToStringInternal(lhs) + ValueToStringInternal(rhs);
        }
        return AsNumber(lhs, "+") + AsNumber(rhs, "+");
    case BinaryOp::Subtract:
        if (TryCallOperator(lhs, "__sub__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rsub__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "-") - AsNumber(rhs, "-");
    case BinaryOp::Multiply:
        if (TryCallOperator(lhs, "__mul__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmul__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "*") * AsNumber(rhs, "*");
    case BinaryOp::Divide: {
        if (TryCallOperator(lhs, "__div__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rdiv__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "/");
        if (rhs_number == 0.0) {
            throw IRException("division by zero");
        }
        return AsNumber(lhs, "/") / rhs_number;
    }
    case BinaryOp::IntDivide: {
        if (TryCallOperator(lhs, "__floordiv__", {rhs}, env,
                            overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rfloordiv__", {lhs}, env,
                            overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "//");
        if (rhs_number == 0.0) {
            throw IRException("integer division by zero");
        }
        return std::floor(AsNumber(lhs, "//") / rhs_number);
    }
    case BinaryOp::Modulo: {
        if (TryCallOperator(lhs, "__mod__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmod__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "%");
        if (rhs_number == 0.0) {
            throw IRException("modulo by zero");
        }
        return std::fmod(AsNumber(lhs, "%"), rhs_number);
    }
    case BinaryOp::Pow:
        if (TryCallOperator(lhs, "__pow__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rpow__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return std::pow(AsNumber(lhs, "**"), AsNumber(rhs, "**"));
    case BinaryOp::BitwiseAnd:
        return static_cast<double>(AsInt64(lhs, "&") & AsInt64(rhs, "&"));
    case BinaryOp::BitwiseOr:
        return static_cast<double>(AsInt64(lhs, "|") | AsInt64(rhs, "|"));
    case BinaryOp::BitwiseXor:
        return static_cast<double>(AsInt64(lhs, "^") ^ AsInt64(rhs, "^"));
    case BinaryOp::ShiftLeft: {
        const std::int64_t shift = AsInt64(rhs, "<<");
        if (shift < 0 || shift > 63) {
            throw IRException("shift count out of range in <<");
        }
        return static_cast<double>(AsInt64(lhs, "<<") << shift);
    }
    case BinaryOp::ShiftRight: {
        const std::int64_t shift = AsInt64(rhs, ">>");
        if (shift < 0 || shift > 63) {
            throw IRException("shift count out of range in >>");
        }
        return static_cast<double>(AsInt64(lhs, ">>") >> shift);
    }
    case BinaryOp::Equal: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        return ValueEquals(lhs, rhs);
    }
    case BinaryOp::NotEqual: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__ne__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, overloaded)) {
            return !overloaded;
        }
        return !ValueEquals(lhs, rhs);
    }
    case BinaryOp::Less: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__lt__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        return ValueLess(lhs, rhs);
    }
    case BinaryOp::LessEqual: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__le__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        bool lt_result = false;
        if (TryCallOperatorBool(lhs, "__lt__", {rhs}, env, lt_result) &&
            lt_result) {
            return true;
        }
        bool eq_result = false;
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, eq_result)) {
            return eq_result;
        }
        return ValueLess(lhs, rhs) || ValueEquals(lhs, rhs);
    }
    case BinaryOp::Greater: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__gt__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        return ValueLess(rhs, lhs);
    }
    case BinaryOp::GreaterEqual: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__ge__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        bool gt_result = false;
        if (TryCallOperatorBool(lhs, "__gt__", {rhs}, env, gt_result) &&
            gt_result) {
            return true;
        }
        bool eq_result = false;
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, eq_result)) {
            return eq_result;
        }
        return ValueLess(rhs, lhs) || ValueEquals(lhs, rhs);
    }
    }

    throw IRException("unsupported binary operation");
}

Value ExecuteFunction(const Function &function, RuntimeEnvironment &env,
                      const std::vector<Value> &args,
                      const ObjectInstancePtr &self_object) {
    if (function.params.size() != args.size()) {
        throw IRException("function '" + function.name + "' expects " +
                          std::to_string(function.params.size()) +
                          " argument(s), got " + std::to_string(args.size()));
    }

    ValidateFunctionLayout(function);

    std::vector<Value> locals(function.next_slot, std::monostate{});
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        const SlotId slot = function.param_slots[i];
        if (slot >= locals.size()) {
            throw IRException("invalid parameter slot in function '" +
                              function.name + "'");
        }
        StoreLocal(locals, slot, args[i], function, env);
    }
    if (self_object) {
        for (SlotId slot = 0; slot < function.slot_names.size(); ++slot) {
            if (function.slot_names[slot] == "self") {
                StoreLocal(locals, slot, self_object, function, env);
            }
        }
    }

    std::unordered_map<LocalId, Value> temps;
    BlockId current = function.entry;

    while (true) {
        if (current >= function.blocks.size()) {
            throw IRException("invalid block jump in function '" +
                              function.name + "'");
        }

        const BasicBlock &block = function.blocks[current];
        for (const Instruction &instruction : block.instructions) {
            std::visit(
                [&](const auto &inst) {
                    using T = std::decay_t<decltype(inst)>;
                    if constexpr (std::is_same_v<T, LoadLocalInst>) {
                        temps[inst.dst] =
                            LoadLocal(locals, inst.slot, function.name);
                    } else if constexpr (std::is_same_v<T, StoreLocalInst>) {
                        StoreLocal(locals, inst.slot,
                                   EvalValueRef(inst.value, temps), function,
                                   env);
                    } else if constexpr (std::is_same_v<T, DeclareGlobalInst>) {
                        DeclareGlobal(inst.name, inst.type_name,
                                      EvalValueRef(inst.value, temps), env);
                    } else if constexpr (std::is_same_v<T, LoadGlobalInst>) {
                        temps[inst.dst] = LookupGlobal(inst.name, env);
                    } else if constexpr (std::is_same_v<T, StoreGlobalInst>) {
                        StoreGlobal(inst.name, EvalValueRef(inst.value, temps),
                                    env);
                    } else if constexpr (std::is_same_v<T, MoveInst>) {
                        temps[inst.dst] = EvalValueRef(inst.src, temps);
                    } else if constexpr (std::is_same_v<T, UnaryInst>) {
                        temps[inst.dst] = ApplyUnary(
                            inst.op, EvalValueRef(inst.value, temps), env);
                    } else if constexpr (std::is_same_v<T, BinaryInst>) {
                        temps[inst.dst] =
                            ApplyBinary(inst.op, EvalValueRef(inst.lhs, temps),
                                        EvalValueRef(inst.rhs, temps), env);
                    } else if constexpr (std::is_same_v<T, MakeArrayInst>) {
                        auto array = std::make_shared<ArrayInstance>();
                        array->elements.reserve(inst.elements.size());
                        for (const ValueRef &value : inst.elements) {
                            array->elements.push_back(
                                EvalValueRef(value, temps));
                        }
                        temps[inst.dst] = array;
                    } else if constexpr (std::is_same_v<T, ArrayLoadInst>) {
                        const Value object_value =
                            EvalValueRef(inst.array, temps);
                        const std::size_t at = AsIndex(
                            EvalValueRef(inst.index, temps), "index access");

                        if (const auto *array =
                                std::get_if<ArrayInstancePtr>(&object_value)) {
                            if (!*array) {
                                throw IRException("index access on null array");
                            }
                            if (at >= (*array)->elements.size()) {
                                throw IRException(
                                    "index access out of range: " +
                                    std::to_string(at));
                            }
                            temps[inst.dst] = (*array)->elements[at];
                            return;
                        }
                        if (const auto *text =
                                std::get_if<std::string>(&object_value)) {
                            if (at >= text->size()) {
                                throw IRException(
                                    "string index out of range: " +
                                    std::to_string(at));
                            }
                            temps[inst.dst] = static_cast<char>((*text)[at]);
                            return;
                        }

                        throw IRException(
                            "index access requires an array or string value");
                    } else if constexpr (std::is_same_v<T, ArrayStoreInst>) {
                        const Value object_value =
                            EvalValueRef(inst.array, temps);
                        const auto *array =
                            std::get_if<ArrayInstancePtr>(&object_value);
                        if (array == nullptr || !*array) {
                            throw IRException(
                                "index assignment requires an array value");
                        }
                        const std::size_t at =
                            AsIndex(EvalValueRef(inst.index, temps),
                                    "index assignment");
                        if (at >= (*array)->elements.size()) {
                            throw IRException(
                                "index assignment out of range: " +
                                std::to_string(at));
                        }
                        (*array)->elements[at] =
                            EvalValueRef(inst.value, temps);
                    } else if constexpr (std::is_same_v<
                                             T, ResolveFieldOffsetInst>) {
                        const Value object_value =
                            EvalValueRef(inst.object, temps);
                        const auto *object =
                            std::get_if<ObjectInstancePtr>(&object_value);
                        if (object == nullptr || !*object) {
                            throw IRException("field offset resolution "
                                              "requires an object value");
                        }
                        const SlotId offset = ResolveObjectFieldOffset(
                            *object, inst.member, "fldoff");
                        temps[inst.dst] = static_cast<double>(offset);
                    } else if constexpr (std::is_same_v<T, ObjectLoadInst>) {
                        const Value object_value =
                            EvalValueRef(inst.object, temps);
                        const auto *object =
                            std::get_if<ObjectInstancePtr>(&object_value);
                        if (object == nullptr || !*object) {
                            throw IRException(
                                "object load requires an object value");
                        }
                        const std::size_t at = AsIndex(
                            EvalValueRef(inst.offset, temps), "object load");
                        if (at >= (*object)->memory.size()) {
                            throw IRException(
                                "object load offset out of range: " +
                                std::to_string(at));
                        }
                        temps[inst.dst] = (*object)->memory[at];
                    } else if constexpr (std::is_same_v<T, ObjectStoreInst>) {
                        const Value object_value =
                            EvalValueRef(inst.object, temps);
                        const auto *object =
                            std::get_if<ObjectInstancePtr>(&object_value);
                        if (object == nullptr || !*object) {
                            throw IRException(
                                "object store requires an object value");
                        }
                        const std::size_t at = AsIndex(
                            EvalValueRef(inst.offset, temps), "object store");
                        if (at >= (*object)->memory.size()) {
                            throw IRException(
                                "object store offset out of range: " +
                                std::to_string(at));
                        }
                        const ClassInfo &layout =
                            RequireObjectLayout(*object, "object store");
                        std::string field_name = "#" + std::to_string(at);
                        std::string field_type;
                        for (std::size_t field_index = 0;
                             field_index < layout.fields.size(); ++field_index) {
                            const auto offset_it =
                                layout.field_offsets.find(layout.fields[field_index]);
                            if (offset_it == layout.field_offsets.end() ||
                                offset_it->second != at) {
                                continue;
                            }
                            field_name = layout.fields[field_index];
                            if (field_index < layout.field_types.size()) {
                                field_type = layout.field_types[field_index];
                            }
                            break;
                        }
                        const Value field_value =
                            EvalValueRef(inst.value, temps);
                        (*object)->memory[at] = ValidateValueForDeclaredType(
                            field_value, field_type, env,
                            "assignment to field '" + layout.name + "." +
                                field_name + "'");
                    } else if constexpr (std::is_same_v<
                                             T, ResolveMethodSlotInst>) {
                        const Value object_value =
                            EvalValueRef(inst.object, temps);
                        const auto *object =
                            std::get_if<ObjectInstancePtr>(&object_value);
                        if (object == nullptr || !*object) {
                            throw IRException("method slot resolution requires "
                                              "an object value");
                        }
                        const SlotId slot = ResolveObjectMethodSlot(
                            *object, inst.method, "mslot");
                        temps[inst.dst] = static_cast<double>(slot);
                    } else if constexpr (std::is_same_v<T, CallInst>) {
                        std::vector<Value> call_args;
                        call_args.reserve(inst.args.size());
                        for (const ValueRef &arg : inst.args) {
                            call_args.push_back(EvalValueRef(arg, temps));
                        }

                        const auto user_it = env.functions.find(inst.callee);
                        if (user_it != env.functions.end()) {
                            temps[inst.dst] = ExecuteFunction(*user_it->second,
                                                              env, call_args);
                            return;
                        }

                        const auto class_it = env.classes.find(inst.callee);
                        if (class_it != env.classes.end()) {
                            const ClassInfo &class_info = *class_it->second;
                            throw IRException("type '" + class_info.name +
                                              "' is not callable; use 'new " +
                                              class_info.name +
                                              "(...)' for construction");
                        }

                        if (IsBuiltinFunction(inst.callee)) {
                            temps[inst.dst] =
                                CallBuiltin(inst.callee, call_args, env);
                            return;
                        }

                        throw IRException("unknown function or type: " +
                                          inst.callee);
                    } else if constexpr (std::is_same_v<T, VirtualCallInst>) {
                        const Value object_value =
                            EvalValueRef(inst.object, temps);
                        const auto *object =
                            std::get_if<ObjectInstancePtr>(&object_value);
                        if (object == nullptr || !*object) {
                            throw IRException(
                                "virtual call requires an object value");
                        }

                        const std::size_t slot = AsIndex(
                            EvalValueRef(inst.slot, temps), "virtual call");
                        if (slot >= (*object)->vtable.size()) {
                            throw IRException(
                                "virtual call slot out of range: " +
                                std::to_string(slot));
                        }
                        const Function *method = (*object)->vtable[slot];
                        if (method == nullptr) {
                            throw IRException(
                                "virtual call through null method slot");
                        }

                        std::vector<Value> call_args;
                        call_args.reserve(inst.args.size());
                        for (const ValueRef &arg : inst.args) {
                            call_args.push_back(EvalValueRef(arg, temps));
                        }

                        temps[inst.dst] =
                            CallMethod(*object, *method, call_args, env);
                    }
                },
                instruction);
        }

        if (!block.terminator.has_value()) {
            throw IRException("basic block b" + std::to_string(block.id) +
                              " in function '" + function.name +
                              "' has no terminator");
        }

        const Terminator &term = *block.terminator;
        if (const auto *jump = std::get_if<JumpTerm>(&term)) {
            current = jump->target;
            continue;
        }
        if (const auto *branch = std::get_if<BranchTerm>(&term)) {
            current = IsTruthy(EvalValueRef(branch->condition, temps))
                          ? branch->true_target
                          : branch->false_target;
            continue;
        }
        if (const auto *ret = std::get_if<ReturnTerm>(&term)) {
            if (ret->value.has_value()) {
                return EvalValueRef(*ret->value, temps);
            }
            return std::monostate{};
        }

        throw IRException("unknown terminator kind");
    }
}

void RegisterProgramDeclarations(const Program &program,
                                 RuntimeEnvironment &env,
                                 const std::string_view unit_name) {
    for (const Function &function : program.functions) {
        if (function.name == program.entry_function) {
            continue;
        }
        if (!env.functions.emplace(function.name, &function).second) {
            throw IRException(
                "duplicate function declaration: " + function.name + " (" +
                std::string(unit_name) + ")");
        }
    }

    for (const ClassInfo &class_info : program.classes) {
        if (!env.classes.emplace(class_info.name, &class_info).second) {
            throw IRException(
                "duplicate class declaration: " + class_info.name + " (" +
                std::string(unit_name) + ")");
        }
        if (class_info.field_types.size() != class_info.fields.size()) {
            throw IRException("class '" + class_info.name +
                              "' has mismatched field_types and fields");
        }

        auto &method_map = env.class_methods[class_info.name];
        auto &method_slots = env.class_method_slots[class_info.name];
        std::vector<const Function *> vtable(class_info.vtable_functions.size(),
                                             nullptr);

        for (const auto &[method_name, method_slot] : class_info.method_slots) {
            if (method_slot >= class_info.vtable_functions.size()) {
                throw IRException("invalid vtable slot for method '" +
                                  class_info.name + "." + method_name + "'");
            }
            const std::string &method_fn_name =
                class_info.vtable_functions[method_slot];
            const auto fn_it = env.functions.find(method_fn_name);
            if (fn_it == env.functions.end()) {
                throw IRException("class method target function is missing: " +
                                  class_info.name + "." + method_name + " -> " +
                                  method_fn_name);
            }
            if (!method_map.emplace(method_name, fn_it->second).second) {
                throw IRException(
                    "duplicate method declaration: " + class_info.name + "." +
                    method_name + " (" + std::string(unit_name) + ")");
            }
            if (!method_slots.emplace(method_name, method_slot).second) {
                throw IRException(
                    "duplicate method slot declaration: " + class_info.name +
                    "." + method_name + " (" + std::string(unit_name) + ")");
            }
            if (vtable[method_slot] != nullptr) {
                throw IRException(
                    "duplicate vtable slot assignment for class '" +
                    class_info.name + "'");
            }
            vtable[method_slot] = fn_it->second;
        }

        for (std::size_t slot = 0; slot < vtable.size(); ++slot) {
            if (vtable[slot] == nullptr) {
                throw IRException("missing vtable entry " +
                                  std::to_string(slot) + " for class '" +
                                  class_info.name + "'");
            }
        }
        env.class_vtables[class_info.name] = std::move(vtable);

        if (!class_info.constructor_function.empty()) {
            const auto ctor_it =
                env.functions.find(class_info.constructor_function);
            if (ctor_it == env.functions.end()) {
                throw IRException(
                    "class constructor target function is missing: " +
                    class_info.name + " -> " + class_info.constructor_function);
            }
            if (!env.class_constructors
                     .emplace(class_info.name, ctor_it->second)
                     .second) {
                throw IRException(
                    "duplicate constructor declaration: " + class_info.name +
                    " (" + std::string(unit_name) + ")");
            }
        }
    }
}

Value ExecuteTopLevel(const Program &program, RuntimeEnvironment &env,
                      const std::string_view unit_name,
                      const bool allow_top_level_return) {
    const Function *entry_function = nullptr;
    for (const Function &function : program.functions) {
        if (function.name == program.entry_function) {
            entry_function = &function;
            break;
        }
    }
    if (entry_function == nullptr) {
        throw IRException("entry function '" + program.entry_function +
                          "' not found while executing " +
                          std::string(unit_name));
    }

    Value result = ExecuteFunction(*entry_function, env, {});
    if (!allow_top_level_return) {
        (void)result;
        return std::monostate{};
    }
    return result;
}

} // namespace

std::string ValueToString(const Value &value) {
    return ValueToStringInternal(value);
}

std::string ProgramToAssembly(const Program &program,
                              const std::string_view unit_name) {
    std::ostringstream out;
    out << "; IR unit: " << unit_name << "\n";

    if (!program.classes.empty()) {
        out << "; Classes\n";
        for (const ClassInfo &class_info : program.classes) {
            out << ".class " << class_info.name;
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
                out << "  .ctor -> " << class_info.constructor_function << "\n";
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
                out << "  .vslot " << slot;
                if (!method_name.empty()) {
                    out << " " << method_name;
                }
                out << " -> " << class_info.vtable_functions[slot] << "\n";
            }
        }
        out << "\n";
    }

    out << "; Functions\n";
    for (const Function &function : program.functions) {
        out << "func " << function.name << "(";
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << function.params[i];
            if (i < function.param_types.size() &&
                !function.param_types[i].empty()) {
                out << ":" << function.param_types[i];
            }
        }
        out << ")";
        if (function.is_method) {
            out << " ; method " << function.owning_class << "."
                << function.method_name;
        }
        out << "\n";
        if (!function.param_slots.empty()) {
            out << "  .param_slots ";
            for (std::size_t i = 0; i < function.param_slots.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << function.params[i] << "=s" << function.param_slots[i];
                if (i < function.param_types.size() &&
                    !function.param_types[i].empty()) {
                    out << ":" << function.param_types[i];
                }
            }
            out << "\n";
        }
        if (!function.slot_names.empty()) {
            out << "  .locals ";
            for (SlotId slot = 0; slot < function.slot_names.size(); ++slot) {
                if (slot != 0) {
                    out << ", ";
                }
                out << "s" << slot;
                if (!function.slot_names[slot].empty()) {
                    out << "(" << function.slot_names[slot] << ")";
                }
                if (slot < function.slot_types.size() &&
                    !function.slot_types[slot].empty()) {
                    out << ":" << function.slot_types[slot];
                }
            }
            out << "\n";
        }

        for (const BasicBlock &block : function.blocks) {
            out << "  b" << block.id;
            if (!block.label.empty()) {
                out << " ; " << block.label;
            }
            out << "\n";
            for (const Instruction &instruction : block.instructions) {
                out << "    " << InstructionToAssembly(instruction) << "\n";
            }
            if (block.terminator.has_value()) {
                out << "    " << TerminatorToAssembly(*block.terminator)
                    << "\n";
            } else {
                out << "    ; <missing terminator>\n";
            }
        }
        out << "endfunc\n\n";
    }

    return out.str();
}

std::vector<std::string> ListOptimizationPasses() {
    std::vector<std::string> names;
    std::vector<std::unique_ptr<Pass>> passes = BuildDefaultPassPipeline();
    names.reserve(passes.size());
    for (const std::unique_ptr<Pass> &pass : passes) {
        names.emplace_back(pass->Name());
    }
    return names;
}

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
    if (out.size() > 4 &&
        out.substr(out.size() - 4) == "pass") {
        out.resize(out.size() - 4);
    }
    return out;
}

} // namespace

void OptimizeProgram(Program &program, const OptimizationOptions &options) {
    std::vector<std::unique_ptr<Pass>> passes = BuildDefaultPassPipeline();
    std::unordered_map<std::string, std::string> canonical_to_name;
    std::vector<std::string> available_names;
    canonical_to_name.reserve(passes.size());
    available_names.reserve(passes.size());
    for (const std::unique_ptr<Pass> &pass : passes) {
        available_names.emplace_back(pass->Name());
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
            for (const std::string &name : available_names) {
                if (!available.empty()) {
                    available += ", ";
                }
                available += name;
            }
            throw IRException("unknown optimization pass '" + pass_name +
                              "'. Available passes: " + available);
        }
        disabled.insert(canonical);
    }

    const int max_rounds = std::max(1, options.max_rounds);
    for (int round = 0; round < max_rounds; ++round) {
        bool changed = false;
        for (std::unique_ptr<Pass> &pass : passes) {
            if (disabled.contains(CanonicalizePassName(pass->Name()))) {
                continue;
            }
            changed |= pass->Run(program);
        }
        if (!changed) {
            break;
        }
    }
}

void OptimizeProgram(Program &program) {
    OptimizeProgram(program, OptimizationOptions{});
}

Value ExecuteProgram(const Program &program,
                     const std::vector<ProgramUnit> &prelude_units) {
    RuntimeEnvironment env;
    env.globals["pi"] = RuntimeEnvironment::GlobalBinding{
        .value = std::acos(-1.0),
        .type_name = "number",
    };
    env.globals["e"] = RuntimeEnvironment::GlobalBinding{
        .value = std::exp(1.0),
        .type_name = "number",
    };
    env.globals["tau"] = RuntimeEnvironment::GlobalBinding{
        .value = 2.0 * std::acos(-1.0),
        .type_name = "number",
    };

    for (const ProgramUnit &unit : prelude_units) {
        RegisterProgramDeclarations(unit.program, env, unit.name);
    }

    RegisterProgramDeclarations(program, env, "<program>");

    for (const ProgramUnit &unit : prelude_units) {
        (void)ExecuteTopLevel(unit.program, env, unit.name, false);
    }

    return ExecuteTopLevel(program, env, "<program>", true);
}

} // namespace compiler::ir
