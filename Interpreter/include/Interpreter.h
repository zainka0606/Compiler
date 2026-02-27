#pragma once

#include "AST.h"
#include "CFG.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace compiler::interpreter {

class InterpreterException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct SymbolTable {
    std::unordered_set<std::string> functions;
    std::unordered_set<std::string> globals;
};

struct ProgramAnnotation {
    std::vector<std::string> flattened_items;
    SymbolTable symbols;
    std::unordered_map<std::string, std::vector<std::string>> function_parameters;
    std::unordered_map<std::string, std::vector<std::string>> function_statements;
};

using Value = std::variant<std::monostate, double, std::string, bool, char>;

std::string ValueToString(const Value& value);

AST ParseProgram(std::string_view source_text);
ProgramAnnotation AnnotateProgram(const AST& ast);
Value InterpretProgram(const AST& ast);
Value InterpretSource(std::string_view source_text);

} // namespace compiler::interpreter
