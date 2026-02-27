#pragma once

#include "AST.h"
#include "CFG.h"
#include "SymbolTable.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace compiler::interpreter {

class InterpreterException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct ProgramAnnotation {
    std::vector<std::string> flattened_items;
    SymbolTable symbols;
    std::unordered_map<std::string, std::vector<std::string>>
        function_parameters;
    std::unordered_map<std::string, std::vector<std::string>>
        function_statements;
    std::unordered_map<std::string, std::vector<std::string>> class_fields;
    std::unordered_map<std::string, std::vector<std::string>> class_methods;
};

struct ObjectInstance;
struct ArrayInstance;
using ObjectInstancePtr = std::shared_ptr<ObjectInstance>;
using ArrayInstancePtr = std::shared_ptr<ArrayInstance>;
using Value = std::variant<std::monostate, double, std::string, bool, char, ObjectInstancePtr, ArrayInstancePtr>;

struct ObjectInstance {
    std::string class_name;
    std::unordered_map<std::string, Value> fields;
};

struct ArrayInstance {
    std::vector<Value> elements;
};

std::string ValueToString(const Value &value);

AST ParseProgram(std::string_view source_text);
ProgramAnnotation AnnotateProgram(const AST &ast);
Value InterpretProgram(const AST &ast);
Value InterpretSource(std::string_view source_text);

} // namespace compiler::interpreter
