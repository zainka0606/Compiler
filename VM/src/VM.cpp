#include "VM.h"

#include "Common/NumberParsing.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace compiler::vm {

namespace {

using bytecode::Address;
using bytecode::Instruction;
using bytecode::OpCode;
using bytecode::Operand;
using bytecode::OperandKind;

using ObjectInstance = ir::ObjectInstance;
using ArrayInstance = ir::ArrayInstance;
using ObjectInstancePtr = ir::ObjectInstancePtr;
using ArrayInstancePtr = ir::ArrayInstancePtr;
using Value = ir::Value;

struct RuntimeEnvironment {
    struct GlobalBinding {
        Value value = std::monostate{};
        std::string type_name;
    };

    std::unordered_map<std::string, const bytecode::Function *>
        functions;
    std::unordered_map<std::string, Address> function_addresses;
    std::unordered_map<Address, const bytecode::Function *> functions_by_address;
    std::unordered_map<std::string, const ir::ClassInfo *> classes;
    std::vector<std::unique_ptr<ir::ClassInfo>> class_storage;
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, const bytecode::Function *>>
        class_methods;
    std::unordered_map<std::string, const bytecode::Function *>
        class_constructors;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, ir::SlotId>>
        class_method_slots;
    std::unordered_map<std::string,
                       std::vector<const bytecode::Function *>>
        class_vtables;
    std::unordered_map<std::string, GlobalBinding> globals;
};

std::string NumberToString(const double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

std::string ValueToStringInternal(const Value &value) {
    return ir::ValueToString(value);
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
        throw VMException(ex.what());
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
        throw VMException(std::string("cannot use object as number in ") +
                          std::string(context));
    }
    if (std::holds_alternative<ArrayInstancePtr>(value)) {
        throw VMException(std::string("cannot use array as number in ") +
                          std::string(context));
    }
    throw VMException(std::string("cannot use null value as number in ") +
                      std::string(context));
}

std::int64_t AsInt64(double number, std::string_view context);

std::int64_t AsInt64(const Value &value, const std::string_view context) {
    const double number = AsNumber(value, context);
    return AsInt64(number, context);
}

std::int64_t AsInt64(const double number, const std::string_view context) {
    if (!std::isfinite(number)) {
        throw VMException("integer in " + std::string(context) +
                          " must be finite");
    }
    const double rounded = std::round(number);
    const double tolerance = std::numeric_limits<double>::epsilon() * 64.0 *
                             std::max(1.0, std::abs(number));
    if (std::abs(number - rounded) > tolerance) {
        throw VMException("integer in " + std::string(context) +
                          " must not have a fractional part (got " +
                          NumberToString(number) + ")");
    }
    if (rounded <
            static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        rounded >
            static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        throw VMException("integer in " + std::string(context) +
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
        throw VMException("index in " + std::string(context) +
                          " must be finite");
    }
    if (raw < 0.0) {
        throw VMException("index in " + std::string(context) +
                          " must be non-negative");
    }
    const double truncated = std::floor(raw);
    if (truncated != raw) {
        throw VMException("index in " + std::string(context) +
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

    throw VMException("unknown type '" + normalized + "' in " +
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
        throw VMException(std::string(context) + ": expected type '" +
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
            throw VMException(
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
        throw VMException(std::string(context) + ": cannot cast '" +
                          RuntimeTypeName(value) + "' to 'number'");
    }

    if (resolved == "int") {
        if (std::holds_alternative<double>(value)) {
            const double number = std::get<double>(value);
            if (!IsIntegralNumber(number)) {
                throw VMException(std::string(context) +
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
                throw VMException(std::string(context) +
                                  ": cannot cast non-integral value '" +
                                  NumberToString(number) + "' to 'int'");
            }
            return static_cast<double>(
                static_cast<std::int64_t>(std::round(number)));
        }
        throw VMException(std::string(context) + ": cannot cast '" +
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
                throw VMException(std::string(context) +
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
                throw VMException(std::string(context) +
                                  ": char cast codepoint out of range [0,255]");
            }
            return static_cast<char>(codepoint);
        }
        throw VMException(std::string(context) + ": cannot cast '" +
                          RuntimeTypeName(value) + "' to 'char'");
    }

    if (resolved == "null") {
        if (!std::holds_alternative<std::monostate>(value)) {
            throw VMException(std::string(context) + ": cannot cast '" +
                              RuntimeTypeName(value) + "' to 'null'");
        }
        return value;
    }

    if (resolved == "array") {
        const auto *array = std::get_if<ArrayInstancePtr>(&value);
        if (array == nullptr || !*array) {
            throw VMException(std::string(context) + ": cannot cast '" +
                              RuntimeTypeName(value) + "' to 'array'");
        }
        return value;
    }

    if (resolved == "object") {
        const auto *object = std::get_if<ObjectInstancePtr>(&value);
        if (object == nullptr || !*object) {
            throw VMException(std::string(context) + ": cannot cast '" +
                              RuntimeTypeName(value) + "' to 'object'");
        }
        return value;
    }

    if (IsArrayTypeName(resolved)) {
        const auto *array = std::get_if<ArrayInstancePtr>(&value);
        if (array == nullptr || !*array) {
            throw VMException(std::string(context) + ": cannot cast '" +
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
        throw VMException(std::string(context) + ": cannot cast '" +
                          RuntimeTypeName(value) + "' to '" + resolved + "'");
    }
    return value;
}

Value LookupGlobal(const std::string_view name, const RuntimeEnvironment &env) {
    const auto it = env.globals.find(std::string(name));
    if (it != env.globals.end()) {
        return it->second.value;
    }
    throw VMException("unknown identifier: " + std::string(name));
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
        throw VMException("assignment to unknown identifier: " +
                          std::string(name));
    }
    const std::string context =
        "assignment to global '" + std::string(name) + "'";
    it->second.value =
        ValidateValueForDeclaredType(value, it->second.type_name, env, context);
}

struct CPUFlags {
    bool carry = false;
    bool zero = false;
    bool sign = false;
    bool overflow = false;
};

struct FrameState {
    std::vector<Value> stack;
    std::array<Value, bytecode::kRegisterCount> registers;
    bytecode::SlotId bp = 0;
    bytecode::SlotId sp = 0;
    CPUFlags flags;
};

void SyncPointerRegisters(FrameState &frame) {
    (void)frame;
}

std::size_t EffectiveStackIndex(const FrameState &frame, const bytecode::SlotId slot,
                                const std::string_view function_name) {
    const std::size_t index = static_cast<std::size_t>(frame.bp + slot);
    if (index >= frame.stack.size()) {
        throw VMException("invalid stack slot s" + std::to_string(slot) +
                          " in function '" + std::string(function_name) + "'");
    }
    return index;
}

std::size_t EffectiveAbsoluteStackIndex(const FrameState &frame,
                                        const bytecode::SlotId slot,
                                        const std::string_view function_name) {
    const std::size_t index = static_cast<std::size_t>(slot);
    if (index >= frame.stack.size()) {
        throw VMException("invalid absolute stack slot s" + std::to_string(slot) +
                          " in function '" + std::string(function_name) + "'");
    }
    return index;
}

Value LoadStackSlot(const FrameState &frame, const bytecode::SlotId slot,
                    const std::string_view function_name) {
    return frame.stack[EffectiveStackIndex(frame, slot, function_name)];
}

Value LoadAbsoluteStackSlot(const FrameState &frame, const bytecode::SlotId slot,
                            const std::string_view function_name) {
    return frame.stack[EffectiveAbsoluteStackIndex(frame, slot, function_name)];
}

void StoreStackSlot(FrameState &frame, const bytecode::SlotId slot, Value value,
                    const bytecode::Function &function,
                    RuntimeEnvironment &env) {
    const std::size_t index = EffectiveStackIndex(frame, slot, function.name);
    if (slot < function.local_count) {
        const std::string slot_name =
            slot < function.slot_names.size() && !function.slot_names[slot].empty()
                ? function.slot_names[slot]
                : "s" + std::to_string(slot);
        const std::string slot_type =
            slot < function.slot_types.size() ? function.slot_types[slot]
                                              : std::string{};
        const std::string context = "assignment to local '" + slot_name +
                                    "' in function '" + function.name + "'";
        frame.stack[index] =
            ValidateValueForDeclaredType(value, slot_type, env, context);
        return;
    }
    frame.stack[index] = std::move(value);
}

void StoreAbsoluteStackSlot(FrameState &frame, const bytecode::SlotId slot,
                            Value value, const std::string_view function_name) {
    const std::size_t index =
        EffectiveAbsoluteStackIndex(frame, slot, function_name);
    frame.stack[index] = std::move(value);
}

void ValidateFunctionLayout(const bytecode::Function &function) {
    if (function.param_slots.size() != function.params.size()) {
        throw VMException("function '" + function.name +
                          "' has mismatched param_slots and params");
    }
    if (function.param_types.size() != function.params.size()) {
        throw VMException("function '" + function.name +
                          "' has mismatched param_types and params");
    }
    if (function.local_count != function.slot_names.size()) {
        throw VMException("function '" + function.name +
                          "' has mismatched local_count and slot_names");
    }
    if (function.slot_types.size() != function.slot_names.size()) {
        throw VMException("function '" + function.name +
                          "' has mismatched slot_types and slot_names");
    }
    if (function.stack_slot_count < function.local_count) {
        throw VMException("function '" + function.name +
                          "' has stack_slot_count smaller than local_count");
    }
    for (const ir::SlotId slot : function.param_slots) {
        if (slot >= function.local_count) {
            throw VMException("function '" + function.name +
                              "' has invalid parameter slot");
        }
    }
}

const bytecode::Function *
FindMethodOnClass(const RuntimeEnvironment &env,
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

const ir::ClassInfo &
RequireObjectLayout(const ObjectInstancePtr &object,
                                         const std::string_view context) {
    if (!object) {
        throw VMException(std::string(context) + ": null object");
    }
    if (object->class_layout == nullptr) {
        throw VMException(std::string(context) + ": object of type '" +
                          object->class_name + "' has no class layout");
    }
    return *object->class_layout;
}

ir::SlotId ResolveObjectFieldOffset(const ObjectInstancePtr &object,
                                    const std::string_view field_name,
                                    const std::string_view context) {
    const ir::ClassInfo &layout =
        RequireObjectLayout(object, context);
    const auto field_it = layout.field_offsets.find(std::string(field_name));
    if (field_it == layout.field_offsets.end()) {
        throw VMException(std::string(context) + ": unknown field '" +
                          std::string(field_name) + "' on type '" +
                          object->class_name + "'");
    }
    return field_it->second;
}

ir::SlotId ResolveObjectMethodSlot(const ObjectInstancePtr &object,
                                   const std::string_view method_name,
                                   const std::string_view context) {
    const ir::ClassInfo &layout =
        RequireObjectLayout(object, context);
    const auto method_it = layout.method_slots.find(std::string(method_name));
    if (method_it == layout.method_slots.end()) {
        throw VMException(std::string(context) + ": unknown method '" +
                          std::string(method_name) + "' on type '" +
                          object->class_name + "'");
    }
    return method_it->second;
}

Value ExecuteFunction(const bytecode::Function &function,
                      RuntimeEnvironment &env, const std::vector<Value> &args,
                      const ObjectInstancePtr &self_object = nullptr);

ObjectInstancePtr
CreateObjectInstance(const ir::ClassInfo &class_info,
                     RuntimeEnvironment &env) {
    auto object = std::make_shared<ObjectInstance>();
    object->class_name = class_info.name;
    object->class_layout = &class_info;
    const bool has_vtable = !class_info.vtable_functions.empty();
    const std::size_t object_slots = class_info.fields.size() + 1U;
    object->memory.resize(object_slots, std::monostate{});

    if (has_vtable) {
        const auto vt_it = env.class_vtables.find(class_info.name);
        if (vt_it == env.class_vtables.end()) {
            throw VMException("missing vtable for class '" + class_info.name +
                              "'");
        }
        auto vtable = std::make_shared<ArrayInstance>();
        vtable->elements.reserve(vt_it->second.size());
        for (const bytecode::Function *method : vt_it->second) {
            if (method == nullptr) {
                throw VMException("null virtual method in class '" +
                                  class_info.name + "'");
            }
            const auto address_it = env.function_addresses.find(method->name);
            if (address_it == env.function_addresses.end()) {
                throw VMException("missing function address for virtual method '" +
                                  method->name + "'");
            }
            vtable->elements.push_back(static_cast<double>(address_it->second));
        }
        object->memory[0] = vtable;
    }
    return object;
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
            throw VMException(std::string(context) +
                              " expects a non-null array of bytes");
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve((*array)->elements.size());
        for (const Value &element : (*array)->elements) {
            const std::int64_t raw = AsInt64(element, context);
            if (raw < 0 || raw > 255) {
                throw VMException(std::string(context) +
                                  " byte value must be in range [0, 255]");
            }
            bytes.push_back(static_cast<std::uint8_t>(raw));
        }
        return bytes;
    }
    throw VMException(std::string(context) +
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

Value InstantiateObject(const std::string_view class_name,
                        const std::vector<Value> &ctor_args,
                        RuntimeEnvironment &env,
                        const std::string_view context) {
    if (class_name.empty()) {
        throw VMException(std::string(context) +
                          " expects a non-empty class name");
    }

    const auto class_it = env.classes.find(std::string(class_name));
    if (class_it == env.classes.end()) {
        throw VMException("unknown type in " + std::string(context) + ": " +
                          std::string(class_name));
    }
    const ir::ClassInfo &class_info = *class_it->second;
    ObjectInstancePtr object = CreateObjectInstance(class_info, env);

    const auto ctor_it = env.class_constructors.find(class_info.name);
    if (ctor_it != env.class_constructors.end()) {
        if (ctor_it->second == nullptr) {
            throw VMException("constructor resolution failed for type '" +
                              class_info.name + "'");
        }
        (void)ExecuteFunction(*ctor_it->second, env, ctor_args, object);
        return object;
    }

    if (!ctor_args.empty()) {
        throw VMException("type '" + class_info.name +
                          "' has no constructor; new expects 0 argument(s), got " +
                          std::to_string(ctor_args.size()));
    }
    return object;
}

Value CallBuiltin(std::string_view name, const std::vector<Value> &args,
                  RuntimeEnvironment &env) {
    const auto require_count = [&](const std::size_t expected) {
        if (args.size() != expected) {
            throw VMException("function '" + std::string(name) + "' expects " +
                              std::to_string(expected) + " argument(s), got " +
                              std::to_string(args.size()));
        }
    };
    const auto require_min_count = [&](const std::size_t minimum) {
        if (args.size() < minimum) {
            throw VMException("function '" + std::string(name) +
                              "' expects at least " + std::to_string(minimum) +
                              " argument(s), got " +
                              std::to_string(args.size()));
        }
    };

    if (name == "__cast") {
        require_count(2);
        const auto *type_name = std::get_if<std::string>(&args[1]);
        if (type_name == nullptr) {
            throw VMException(
                "__cast expects a string type name as its second argument");
        }
        return ExplicitCastValue(args[0], *type_name, env, "__cast");
    }

    if (name == "alloc") {
        require_count(1);
        const auto *type_name = std::get_if<std::string>(&args[0]);
        if (type_name == nullptr || type_name->empty()) {
            throw VMException(
                "alloc expects a non-empty class name string as its first argument");
        }
        const auto class_it = env.classes.find(*type_name);
        if (class_it == env.classes.end()) {
            throw VMException("unknown type in alloc: " + *type_name);
        }
        return CreateObjectInstance(*class_it->second, env);
    }

    if (name == "__string_concat") {
        require_count(2);
        return ValueToStringInternal(args[0]) + ValueToStringInternal(args[1]);
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
            throw VMException(
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
            throw VMException("read_file failed to open: " + path);
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
            throw VMException("write_file failed to open: " + path);
        }
        output << content;
        if (!output.good()) {
            throw VMException("write_file failed to write: " + path);
        }
        return static_cast<double>(content.size());
    }

    if (name == "append_file") {
        require_count(2);
        const std::string path = AsString(args[0]);
        const std::string content = AsString(args[1]);
        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output.is_open()) {
            throw VMException("append_file failed to open: " + path);
        }
        output << content;
        if (!output.good()) {
            throw VMException("append_file failed to write: " + path);
        }
        return static_cast<double>(content.size());
    }

    if (name == "read_binary_file") {
        require_count(1);
        const std::string path = AsString(args[0]);
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            throw VMException("read_binary_file failed to open: " + path);
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
            throw VMException("write_binary_file failed to open: " + path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output.good()) {
            throw VMException("write_binary_file failed to write: " + path);
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
            throw VMException("append_binary_file failed to open: " + path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output.good()) {
            throw VMException("append_binary_file failed to write: " + path);
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
            throw VMException("file_size failed for '" + path +
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
                throw VMException("len expects a non-null array");
            }
            return static_cast<double>((*array)->elements.size());
        }
        throw VMException("len expects a string or array");
    }

    if (name == "__array_push") {
        require_count(2);
        const auto *array = std::get_if<ArrayInstancePtr>(&args[0]);
        if (array == nullptr || !*array) {
            throw VMException("__array_push expects an array as first argument");
        }
        (*array)->elements.push_back(args[1]);
        return static_cast<double>((*array)->elements.size());
    }

    if (name == "__array_pop") {
        require_count(1);
        const auto *array = std::get_if<ArrayInstancePtr>(&args[0]);
        if (array == nullptr || !*array) {
            throw VMException("__array_pop expects an array argument");
        }
        if ((*array)->elements.empty()) {
            throw VMException("__array_pop on empty array");
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

    throw VMException("unknown function: " + std::string(name));
}

Value CallMethod(const ObjectInstancePtr &self_object,
                 const bytecode::Function &method,
                 const std::vector<Value> &args, RuntimeEnvironment &env) {
    if (!self_object) {
        throw VMException("cannot call method '" + method.name +
                          "' on null object");
    }
    if (method.params.size() != args.size()) {
        throw VMException("method '" + self_object->class_name + "." +
                          method.name + "' expects " +
                          std::to_string(method.params.size()) +
                          " argument(s), got " + std::to_string(args.size()));
    }
    return ExecuteFunction(method, env, args, self_object);
}

Value InvokeCallableByAddress(const Address callee_address,
                              const std::vector<Value> &args,
                              RuntimeEnvironment &env) {
    if (const std::string_view builtin_name =
            bytecode::BuiltinNameForAddress(callee_address);
        !builtin_name.empty()) {
        return CallBuiltin(builtin_name, args, env);
    }

    const auto user_it = env.functions_by_address.find(callee_address);
    if (user_it == env.functions_by_address.end()) {
        throw VMException("unknown function address @" +
                          std::to_string(callee_address));
    }

    const bytecode::Function &target = *user_it->second;
    if (target.is_method) {
        if (args.empty()) {
            throw VMException("method call to '" + target.name +
                              "' requires object receiver argument");
        }
        const auto *object = std::get_if<ObjectInstancePtr>(&args.front());
        if (object == nullptr || !*object) {
            throw VMException("method call to '" + target.name +
                              "' requires object receiver");
        }
        std::vector<Value> method_args(args.begin() + 1, args.end());
        return CallMethod(*object, target, method_args, env);
    }
    return ExecuteFunction(target, env, args);
}

bool TryCallOperator(const Value &target, const std::string_view method_name,
                     const std::vector<Value> &args, RuntimeEnvironment &env,
                     Value &out) {
    const auto *object = std::get_if<ObjectInstancePtr>(&target);
    if (object == nullptr || !*object) {
        return false;
    }
    const bytecode::Function *method =
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

Value ApplyUnary(const ir::UnaryOp op, const Value &input,
                 RuntimeEnvironment &env) {
    Value overloaded_result;
    switch (op) {
    case ir::UnaryOp::Negate:
        if (TryCallOperator(input, "__neg__", {}, env, overloaded_result)) {
            return overloaded_result;
        }
        return -AsNumber(input, "unary -");
    case ir::UnaryOp::LogicalNot:
        return !IsTruthy(input);
    case ir::UnaryOp::BitwiseNot:
        return static_cast<double>(~AsInt64(input, "~"));
    }
    throw VMException("unsupported unary operation");
}

Value ApplyBinary(const ir::BinaryOp op, const Value &lhs, const Value &rhs,
                  RuntimeEnvironment &env) {
    Value overloaded_result;
    switch (op) {
    case ir::BinaryOp::Add:
        if (TryCallOperator(lhs, "__add__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__radd__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "+") + AsNumber(rhs, "+");
    case ir::BinaryOp::Subtract:
        if (TryCallOperator(lhs, "__sub__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rsub__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "-") - AsNumber(rhs, "-");
    case ir::BinaryOp::Multiply:
        if (TryCallOperator(lhs, "__mul__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmul__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "*") * AsNumber(rhs, "*");
    case ir::BinaryOp::Divide: {
        if (TryCallOperator(lhs, "__div__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rdiv__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "/");
        if (rhs_number == 0.0) {
            throw VMException("division by zero");
        }
        return AsNumber(lhs, "/") / rhs_number;
    }
    case ir::BinaryOp::IntDivide: {
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
            throw VMException("integer division by zero");
        }
        return std::floor(AsNumber(lhs, "//") / rhs_number);
    }
    case ir::BinaryOp::Modulo: {
        if (TryCallOperator(lhs, "__mod__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmod__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "%");
        if (rhs_number == 0.0) {
            throw VMException("modulo by zero");
        }
        return std::fmod(AsNumber(lhs, "%"), rhs_number);
    }
    case ir::BinaryOp::Pow:
        if (TryCallOperator(lhs, "__pow__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rpow__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return std::pow(AsNumber(lhs, "**"), AsNumber(rhs, "**"));
    case ir::BinaryOp::BitwiseAnd:
        return static_cast<double>(AsInt64(lhs, "&") & AsInt64(rhs, "&"));
    case ir::BinaryOp::BitwiseOr:
        return static_cast<double>(AsInt64(lhs, "|") | AsInt64(rhs, "|"));
    case ir::BinaryOp::BitwiseXor:
        return static_cast<double>(AsInt64(lhs, "^") ^ AsInt64(rhs, "^"));
    case ir::BinaryOp::ShiftLeft: {
        const std::int64_t shift = AsInt64(rhs, "<<");
        if (shift < 0 || shift > 63) {
            throw VMException("shift count out of range in <<");
        }
        return static_cast<double>(AsInt64(lhs, "<<") << shift);
    }
    case ir::BinaryOp::ShiftRight: {
        const std::int64_t shift = AsInt64(rhs, ">>");
        if (shift < 0 || shift > 63) {
            throw VMException("shift count out of range in >>");
        }
        return static_cast<double>(AsInt64(lhs, ">>") >> shift);
    }
    case ir::BinaryOp::Equal: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        return ValueEquals(lhs, rhs);
    }
    case ir::BinaryOp::NotEqual: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__ne__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, overloaded)) {
            return !overloaded;
        }
        return !ValueEquals(lhs, rhs);
    }
    case ir::BinaryOp::Less: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__lt__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        return ValueLess(lhs, rhs);
    }
    case ir::BinaryOp::LessEqual: {
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
    case ir::BinaryOp::Greater: {
        bool overloaded = false;
        if (TryCallOperatorBool(lhs, "__gt__", {rhs}, env, overloaded)) {
            return overloaded;
        }
        return ValueLess(rhs, lhs);
    }
    case ir::BinaryOp::GreaterEqual: {
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

    throw VMException("unsupported binary operation");
}

Value EvalOperand(const Operand &operand, const FrameState &frame,
                  const std::string_view function_name) {
    switch (operand.kind) {
    case OperandKind::Register:
        if (operand.reg == bytecode::kStackPointerRegister) {
            return static_cast<double>(frame.sp);
        }
        if (operand.reg == bytecode::kBasePointerRegister) {
            return static_cast<double>(frame.bp);
        }
        if (operand.reg >= frame.registers.size()) {
            throw VMException("invalid register r" +
                              std::to_string(operand.reg) + " in function '" +
                              std::string(function_name) + "'");
        }
        return frame.registers[operand.reg];
    case OperandKind::StackSlot:
        return LoadStackSlot(frame, operand.stack_slot, function_name);
    case OperandKind::Number:
        return operand.number;
    case OperandKind::String:
        return operand.text;
    case OperandKind::Bool:
        return operand.boolean;
    case OperandKind::Char:
        return operand.character;
    case OperandKind::Null:
        return std::monostate{};
    }
    return std::monostate{};
}

Value ImmediateOperandToValue(const Operand &operand,
                              const std::string_view context) {
    switch (operand.kind) {
    case OperandKind::Number:
        return operand.number;
    case OperandKind::String:
        return operand.text;
    case OperandKind::Bool:
        return operand.boolean;
    case OperandKind::Char:
        return operand.character;
    case OperandKind::Null:
        return std::monostate{};
    case OperandKind::Register:
    case OperandKind::StackSlot:
        throw VMException(std::string(context) +
                          " global initializer must be immediate");
    }
    return std::monostate{};
}

void WriteRegister(FrameState &frame, const bytecode::RegisterId dst, Value value,
                   const std::string_view function_name) {
    if (dst == bytecode::kStackPointerRegister) {
        const std::int64_t new_sp_i = AsInt64(value, "write sp");
        if (new_sp_i < 0) {
            throw VMException("stack pointer cannot be negative");
        }
        const auto new_sp = static_cast<std::size_t>(new_sp_i);
        if (new_sp > frame.stack.size()) {
            frame.stack.resize(new_sp, std::monostate{});
        } else if (new_sp < frame.stack.size()) {
            frame.stack.resize(new_sp);
        }
        frame.sp = static_cast<bytecode::SlotId>(new_sp);
        SyncPointerRegisters(frame);
        return;
    }
    if (dst == bytecode::kBasePointerRegister) {
        const std::int64_t new_bp_i = AsInt64(value, "write bp");
        if (new_bp_i < 0) {
            throw VMException("base pointer cannot be negative");
        }
        frame.bp = static_cast<bytecode::SlotId>(new_bp_i);
        SyncPointerRegisters(frame);
        return;
    }
    if (dst >= frame.registers.size()) {
        throw VMException("invalid register write r" + std::to_string(dst) +
                          " in function '" + std::string(function_name) + "'");
    }
    frame.registers[dst] = std::move(value);
}

void WriteResult(FrameState &frame, const bytecode::Instruction &inst, Value value,
                 const bytecode::Function &function, RuntimeEnvironment &env) {
    if (inst.dst != bytecode::kInvalidRegister) {
        WriteRegister(frame, inst.dst, std::move(value), function.name);
        return;
    }
    if (inst.dst_slot != bytecode::kInvalidSlot) {
        StoreStackSlot(frame, inst.dst_slot, std::move(value), function, env);
        return;
    }
}

std::vector<Value> EvaluateOperands(const std::vector<Operand> &operands,
                                    const FrameState &frame,
                                    const std::string_view function_name) {
    std::vector<Value> out;
    out.reserve(operands.size());
    for (const Operand &operand : operands) {
        out.push_back(EvalOperand(operand, frame, function_name));
    }
    return out;
}

CPUFlags ComputeComparisonFlags(const Value &lhs, const Value &rhs) {
    CPUFlags flags;
    flags.zero = ValueEquals(lhs, rhs);
    flags.carry = ValueLess(lhs, rhs);
    flags.sign = ValueLess(lhs, rhs);

    if (IsNumericLike(lhs) && IsNumericLike(rhs)) {
        const double lhs_num = AsNumber(lhs, "cmp");
        const double rhs_num = AsNumber(rhs, "cmp");
        const double diff = lhs_num - rhs_num;
        flags.sign = diff < 0.0;

        if (IsIntegralNumber(lhs_num) && IsIntegralNumber(rhs_num)) {
            const std::int64_t a = AsInt64(lhs_num, "cmp");
            const std::int64_t b = AsInt64(rhs_num, "cmp");
            const std::int64_t result = a - b;
            flags.overflow = ((a ^ b) & (a ^ result)) < 0;
        } else {
            flags.overflow = !std::isfinite(diff);
        }
    }
    return flags;
}

Value ExecuteFunction(const bytecode::Function &function,
                      RuntimeEnvironment &env, const std::vector<Value> &args,
                      const ObjectInstancePtr &self_object) {
    if (function.params.size() != args.size()) {
        throw VMException("function '" + function.name + "' expects " +
                          std::to_string(function.params.size()) +
                          " argument(s), got " + std::to_string(args.size()));
    }

    ValidateFunctionLayout(function);

    FrameState frame;
    frame.bp = 0;
    frame.sp = 0;
    frame.stack.clear();
    frame.registers.fill(std::monostate{});

    const std::size_t reg_arg_count =
        std::min<std::size_t>(args.size(), bytecode::kArgRegisterCount);
    for (std::size_t i = 0; i < reg_arg_count; ++i) {
        frame.registers[i] = args[i];
    }
    for (std::size_t i = reg_arg_count; i < args.size(); ++i) {
        frame.stack.push_back(args[i]);
    }
    frame.sp = static_cast<bytecode::SlotId>(frame.stack.size());

    if (self_object) {
        frame.registers[bytecode::kSelfRegister] = self_object;
    }

    SyncPointerRegisters(frame);

    Address ip = function.entry_pc;
    auto ensure_target = [&](const Address target, const std::string_view kind) {
        if (target >= function.code.size()) {
            throw VMException(std::string(kind) + " target out of range @" +
                              std::to_string(target) + " in function '" +
                              function.name + "'");
        }
    };
    const auto collect_call_args = [&](const Instruction &call_inst)
        -> std::vector<Value> {
        if (!call_inst.operands.empty()) {
            return EvaluateOperands(call_inst.operands, frame, function.name);
        }

        const std::size_t arg_count = static_cast<std::size_t>(call_inst.slot);
        std::vector<Value> out;
        out.reserve(arg_count);

        const std::size_t reg_count =
            std::min<std::size_t>(arg_count, bytecode::kArgRegisterCount);
        for (std::size_t i = 0; i < reg_count; ++i) {
            out.push_back(frame.registers[i]);
        }

        const std::size_t overflow = arg_count - reg_count;
        if (overflow > 0) {
            if (overflow > frame.stack.size()) {
                throw VMException(
                    "call argument overflow exceeds stack size in function '" +
                    function.name + "'");
            }
            const std::size_t start = frame.stack.size() - overflow;
            for (std::size_t i = 0; i < overflow; ++i) {
                out.push_back(frame.stack[start + i]);
            }
        }
        return out;
    };

    while (true) {
        if (ip >= function.code.size()) {
            throw VMException("invalid instruction pointer @" +
                              std::to_string(ip) + " in function '" +
                              function.name + "'");
        }

        const Instruction &inst = function.code[ip];
        switch (inst.opcode) {
        case OpCode::Nop:
            ++ip;
            break;
        case OpCode::Load: {
            Value loaded = std::monostate{};
            switch (inst.load_mode) {
            case bytecode::LoadMode::StackRelative:
                loaded = LoadStackSlot(frame, inst.slot, function.name);
                break;
            case bytecode::LoadMode::StackAbsolute:
                loaded = LoadAbsoluteStackSlot(frame, inst.slot, function.name);
                break;
            case bytecode::LoadMode::Global:
                loaded = LookupGlobal(inst.text, env);
                break;
            case bytecode::LoadMode::ArrayElement: {
                const Value object_value = EvalOperand(inst.a, frame, function.name);
                const std::size_t at = AsIndex(
                    EvalOperand(inst.b, frame, function.name), "index access");
                if (const auto *array =
                        std::get_if<ArrayInstancePtr>(&object_value)) {
                    if (!*array) {
                        throw VMException("index access on null array");
                    }
                    if (at >= (*array)->elements.size()) {
                        throw VMException("index access out of range: " +
                                          std::to_string(at));
                    }
                    loaded = (*array)->elements[at];
                } else if (const auto *text =
                               std::get_if<std::string>(&object_value)) {
                    if (at >= text->size()) {
                        throw VMException("string index out of range: " +
                                          std::to_string(at));
                    }
                    loaded = (*text)[at];
                } else {
                    throw VMException(
                        "index access requires an array or string value");
                }
                break;
            }
            case bytecode::LoadMode::ObjectOffset: {
                const Value object_value = EvalOperand(inst.a, frame, function.name);
                const auto *object = std::get_if<ObjectInstancePtr>(&object_value);
                if (object == nullptr || !*object) {
                    throw VMException("object load requires an object value");
                }
                const std::size_t at = AsIndex(
                    EvalOperand(inst.b, frame, function.name), "object load");
                if (at >= (*object)->memory.size()) {
                    throw VMException("object load offset out of range: " +
                                      std::to_string(at));
                }
                loaded = (*object)->memory[at];
                break;
            }
            case bytecode::LoadMode::FieldOffsetByName: {
                throw VMException(
                    "fieldoff is unsupported in VM; member offsets must be "
                    "resolved at compile time");
                break;
            }
            case bytecode::LoadMode::MethodSlotByName: {
                throw VMException(
                    "mslot is unsupported in VM; virtual slots must be "
                    "resolved at compile time");
                break;
            }
            case bytecode::LoadMode::MethodFunctionBySlot: {
                throw VMException(
                    "mfn is unsupported in VM; load via object/vtable memory "
                    "and callr");
                break;
            }
            }
            WriteResult(frame, inst, std::move(loaded), function, env);
            ++ip;
            break;
        }
        case OpCode::Store: {
            const Value stored_value = EvalOperand(inst.a, frame, function.name);
            switch (inst.store_mode) {
            case bytecode::StoreMode::StackRelative:
                StoreStackSlot(frame, inst.slot, stored_value, function, env);
                break;
            case bytecode::StoreMode::StackAbsolute:
                StoreAbsoluteStackSlot(frame, inst.slot, stored_value,
                                       function.name);
                break;
            case bytecode::StoreMode::Global:
                StoreGlobal(inst.text, stored_value, env);
                break;
            case bytecode::StoreMode::ArrayElement: {
                const Value object_value = EvalOperand(inst.b, frame, function.name);
                const auto *array = std::get_if<ArrayInstancePtr>(&object_value);
                if (array == nullptr || !*array) {
                    throw VMException("index assignment requires an array value");
                }
                const std::size_t at = AsIndex(
                    EvalOperand(inst.c, frame, function.name),
                    "index assignment");
                if (at >= (*array)->elements.size()) {
                    throw VMException("index assignment out of range: " +
                                      std::to_string(at));
                }
                (*array)->elements[at] = stored_value;
                break;
            }
            case bytecode::StoreMode::ObjectOffset: {
                const Value object_value = EvalOperand(inst.b, frame, function.name);
                const auto *object = std::get_if<ObjectInstancePtr>(&object_value);
                if (object == nullptr || !*object) {
                    throw VMException("object store requires an object value");
                }
                const std::size_t at = AsIndex(
                    EvalOperand(inst.c, frame, function.name), "object store");
                if (at >= (*object)->memory.size()) {
                    throw VMException("object store offset out of range: " +
                                      std::to_string(at));
                }
                const ir::ClassInfo &layout =
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
                (*object)->memory[at] = ValidateValueForDeclaredType(
                    stored_value, field_type, env,
                    "assignment to field '" + layout.name + "." + field_name +
                        "'");
                break;
            }
            }
            ++ip;
            break;
        }
        case OpCode::Push:
            frame.stack.push_back(EvalOperand(inst.a, frame, function.name));
            ++frame.sp;
            SyncPointerRegisters(frame);
            ++ip;
            break;
        case OpCode::Pop: {
            if (frame.sp == 0 || frame.stack.empty()) {
                throw VMException("stack underflow in function '" +
                                  function.name + "'");
            }
            Value value = frame.stack.back();
            frame.stack.pop_back();
            --frame.sp;
            SyncPointerRegisters(frame);
            WriteResult(frame, inst, std::move(value), function, env);
            ++ip;
            break;
        }
        case OpCode::DeclareGlobal:
            DeclareGlobal(inst.text, inst.text2,
                          EvalOperand(inst.a, frame, function.name), env);
            ++ip;
            break;
        case OpCode::Move:
            WriteResult(frame, inst, EvalOperand(inst.a, frame, function.name),
                        function, env);
            ++ip;
            break;
        case OpCode::Unary:
            WriteResult(frame, inst,
                        ApplyUnary(inst.unary_op,
                                   EvalOperand(inst.a, frame, function.name), env),
                        function, env);
            ++ip;
            break;
        case OpCode::Binary:
            WriteResult(frame, inst,
                        ApplyBinary(inst.binary_op,
                                    EvalOperand(inst.a, frame, function.name),
                                    EvalOperand(inst.b, frame, function.name),
                                    env),
                        function, env);
            ++ip;
            break;
        case OpCode::Compare:
            frame.flags = ComputeComparisonFlags(
                EvalOperand(inst.a, frame, function.name),
                EvalOperand(inst.b, frame, function.name));
            ++ip;
            break;
        case OpCode::MakeArray: {
            auto array = std::make_shared<ArrayInstance>();
            array->elements.reserve(inst.operands.size());
            for (const Operand &element : inst.operands) {
                array->elements.push_back(
                    EvalOperand(element, frame, function.name));
            }
            WriteResult(frame, inst, array, function, env);
            ++ip;
            break;
        }
        case OpCode::StackAllocObject: {
            if (inst.text.empty()) {
                throw VMException("salloc requires a non-empty class name in '" +
                                  function.name + "'");
            }
            const auto class_it = env.classes.find(inst.text);
            if (class_it == env.classes.end()) {
                throw VMException("unknown type in salloc: " + inst.text);
            }
            Value object = CreateObjectInstance(*class_it->second, env);
            frame.stack.push_back(object);
            ++frame.sp;
            SyncPointerRegisters(frame);
            WriteResult(frame, inst, std::move(object), function, env);
            ++ip;
            break;
        }
        case OpCode::Call: {
            const std::vector<Value> call_args = collect_call_args(inst);
            Value result = InvokeCallableByAddress(inst.target, call_args, env);
            WriteRegister(frame, 0, std::move(result), function.name);
            ++ip;
            break;
        }
        case OpCode::CallRegister: {
            const Value callee_value = EvalOperand(inst.a, frame, function.name);
            const auto *callee_number = std::get_if<double>(&callee_value);
            if (callee_number == nullptr) {
                throw VMException("callr expects a numeric function address");
            }
            const std::int64_t callee_address_i =
                AsInt64(*callee_number, "callr target");
            if (callee_address_i < 0 ||
                static_cast<std::uint64_t>(callee_address_i) >
                    std::numeric_limits<Address>::max()) {
                throw VMException("callr target address out of range");
            }
            const Address callee_address =
                static_cast<Address>(callee_address_i);

            const std::vector<Value> call_args = collect_call_args(inst);
            Value result = InvokeCallableByAddress(callee_address, call_args, env);
            WriteRegister(frame, 0, std::move(result), function.name);
            ++ip;
            break;
        }
        case OpCode::Jump:
            ensure_target(inst.target, "jump");
            ip = inst.target;
            break;
        case OpCode::JumpIfFalse: {
            const bool truthy =
                IsTruthy(EvalOperand(inst.a, frame, function.name));
            if (!truthy) {
                ensure_target(inst.target, "branch");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        }
        case OpCode::JumpCarry:
            if (frame.flags.carry) {
                ensure_target(inst.target, "jc");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpNotCarry:
            if (!frame.flags.carry) {
                ensure_target(inst.target, "jnc");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpZero:
            if (frame.flags.zero) {
                ensure_target(inst.target, "jz");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpNotZero:
            if (!frame.flags.zero) {
                ensure_target(inst.target, "jnz");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpSign:
            if (frame.flags.sign) {
                ensure_target(inst.target, "js");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpNotSign:
            if (!frame.flags.sign) {
                ensure_target(inst.target, "jns");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpOverflow:
            if (frame.flags.overflow) {
                ensure_target(inst.target, "jo");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpNotOverflow:
            if (!frame.flags.overflow) {
                ensure_target(inst.target, "jno");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpAbove:
            if (!frame.flags.carry && !frame.flags.zero) {
                ensure_target(inst.target, "ja");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpAboveEqual:
            if (!frame.flags.carry) {
                ensure_target(inst.target, "jae");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpBelow:
            if (frame.flags.carry) {
                ensure_target(inst.target, "jb");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpBelowEqual:
            if (frame.flags.carry || frame.flags.zero) {
                ensure_target(inst.target, "jbe");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpGreater:
            if (!frame.flags.zero && frame.flags.sign == frame.flags.overflow) {
                ensure_target(inst.target, "jg");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpGreaterEqual:
            if (frame.flags.sign == frame.flags.overflow) {
                ensure_target(inst.target, "jge");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpLess:
            if (frame.flags.sign != frame.flags.overflow) {
                ensure_target(inst.target, "jl");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::JumpLessEqual:
            if (frame.flags.zero || frame.flags.sign != frame.flags.overflow) {
                ensure_target(inst.target, "jle");
                ip = inst.target;
            } else {
                ++ip;
            }
            break;
        case OpCode::Return:
            return frame.registers[0];
        }
    }
}

void RegisterProgramDeclarations(const bytecode::Program &program,
                                 RuntimeEnvironment &env,
                                 const std::string_view unit_name,
                                 Address &next_function_address) {
    for (const bytecode::Function &function : program.functions) {
        if (function.name == program.entry_function) {
            continue;
        }
        if (!env.functions.emplace(function.name, &function).second) {
            throw VMException(
                "duplicate function declaration: " + function.name + " (" +
                std::string(unit_name) + ")");
        }
        if (next_function_address >= bytecode::kBuiltinAddressBase) {
            throw VMException("user function address space exhausted");
        }
        if (!env.function_addresses
                 .emplace(function.name, next_function_address)
                 .second) {
            throw VMException("duplicate function address symbol: " +
                              function.name + " (" +
                              std::string(unit_name) + ")");
        }
        if (!env.functions_by_address
                 .emplace(next_function_address, &function)
                 .second) {
            throw VMException("duplicate function address @" +
                              std::to_string(next_function_address) + " (" +
                              function.name + ")");
        }
        ++next_function_address;
    }

    for (const bytecode::ClassInfo &class_info_bc : program.classes) {
        if (env.classes.contains(class_info_bc.name)) {
            throw VMException(
                "duplicate class declaration: " + class_info_bc.name + " (" +
                std::string(unit_name) + ")");
        }

        auto class_layout = std::make_unique<ir::ClassInfo>();
        class_layout->name = class_info_bc.name;
        class_layout->base_class = class_info_bc.base_class;
        class_layout->fields = class_info_bc.fields;
        class_layout->field_types = class_info_bc.field_types;
        class_layout->field_offsets = class_info_bc.field_offsets;
        class_layout->method_slots = class_info_bc.method_slots;
        class_layout->vtable_functions = class_info_bc.vtable_functions;
        class_layout->method_functions = class_info_bc.method_functions;
        class_layout->constructor_function = class_info_bc.constructor_function;

        if (class_layout->field_types.size() != class_layout->fields.size()) {
            throw VMException("class '" + class_layout->name +
                              "' has mismatched field_types and fields");
        }

        const ir::ClassInfo *class_ptr = class_layout.get();
        env.class_storage.push_back(std::move(class_layout));
        env.classes[class_info_bc.name] = class_ptr;

        auto &method_map = env.class_methods[class_info_bc.name];
        auto &method_slots = env.class_method_slots[class_info_bc.name];
        std::vector<const bytecode::Function *> vtable(
            class_info_bc.vtable_functions.size(), nullptr);

        for (const auto &[method_name, method_slot] :
             class_info_bc.method_slots) {
            if (method_slot >= class_info_bc.vtable_functions.size()) {
                throw VMException("invalid vtable slot for method '" +
                                  class_info_bc.name + "." + method_name + "'");
            }
            const std::string &method_fn_name =
                class_info_bc.vtable_functions[method_slot];
            const auto fn_it = env.functions.find(method_fn_name);
            if (fn_it == env.functions.end()) {
                throw VMException("class method target function is missing: " +
                                  class_info_bc.name + "." + method_name +
                                  " -> " + method_fn_name);
            }
            if (!method_map.emplace(method_name, fn_it->second).second) {
                throw VMException(
                    "duplicate method declaration: " + class_info_bc.name +
                    "." + method_name + " (" + std::string(unit_name) + ")");
            }
            if (!method_slots.emplace(method_name, method_slot).second) {
                throw VMException(
                    "duplicate method slot declaration: " + class_info_bc.name +
                    "." + method_name + " (" + std::string(unit_name) + ")");
            }
            if (vtable[method_slot] != nullptr) {
                throw VMException(
                    "duplicate vtable slot assignment for class '" +
                    class_info_bc.name + "'");
            }
            vtable[method_slot] = fn_it->second;
        }

        for (std::size_t slot = 0; slot < vtable.size(); ++slot) {
            if (vtable[slot] == nullptr) {
                throw VMException("missing vtable entry " +
                                  std::to_string(slot) + " for class '" +
                                  class_info_bc.name + "'");
            }
        }
        env.class_vtables[class_info_bc.name] = std::move(vtable);

        if (!class_info_bc.constructor_function.empty()) {
            const auto ctor_it =
                env.functions.find(class_info_bc.constructor_function);
            if (ctor_it == env.functions.end()) {
                throw VMException(
                    "class constructor target function is missing: " +
                    class_info_bc.name + " -> " +
                    class_info_bc.constructor_function);
            }
            if (!env.class_constructors
                     .emplace(class_info_bc.name, ctor_it->second)
                     .second) {
                throw VMException(
                    "duplicate constructor declaration: " + class_info_bc.name +
                    " (" + std::string(unit_name) + ")");
            }
        }
    }

    for (const bytecode::GlobalVariable &global : program.globals) {
        const std::string context =
            "global initializer '" + global.name + "' in " +
            std::string(unit_name);
        DeclareGlobal(global.name, global.type_name,
                      ImmediateOperandToValue(global.value, context), env);
    }
}

Value ExecuteTopLevel(const bytecode::Program &program,
                      RuntimeEnvironment &env,
                      const std::string_view unit_name,
                      const bool allow_top_level_return) {
    const bytecode::Function *entry_function = nullptr;
    for (const bytecode::Function &function : program.functions) {
        if (function.name == program.entry_function) {
            entry_function = &function;
            break;
        }
    }
    if (entry_function == nullptr) {
        throw VMException("entry function '" + program.entry_function +
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

Value ExecuteProgram(
    const bytecode::Program &program,
    const std::vector<bytecode::ProgramUnit> &prelude_units) {
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

    Address next_function_address = 0;
    for (const bytecode::ProgramUnit &unit : prelude_units) {
        RegisterProgramDeclarations(unit.program, env, unit.name,
                                    next_function_address);
    }

    RegisterProgramDeclarations(program, env, "<program>",
                                next_function_address);

    for (const bytecode::ProgramUnit &unit : prelude_units) {
        (void)ExecuteTopLevel(unit.program, env, unit.name, false);
    }

    return ExecuteTopLevel(program, env, "<program>", true);
}

std::string ValueToString(const Value &value) {
    return ir::ValueToString(value);
}

} // namespace compiler::vm
