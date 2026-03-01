#pragma once

#include "AST.h"
#include "CFG.h"
#include "IR.h"
#include "SymbolTable.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

using ObjectInstance = compiler::ir::ObjectInstance;
using ArrayInstance = compiler::ir::ArrayInstance;
using ObjectInstancePtr = compiler::ir::ObjectInstancePtr;
using ArrayInstancePtr = compiler::ir::ArrayInstancePtr;
using Value = compiler::ir::Value;

std::string ValueToString(const Value &value);

AST ParseProgram(std::string_view source_text);
ProgramAnnotation AnnotateProgram(const AST &ast);
compiler::ir::Program CompileProgramToIR(const AST &ast);
std::vector<compiler::ir::ProgramUnit> CompileStandardLibraryToIR();
Value ExecuteIRProgram(compiler::ir::Program program,
                       std::vector<compiler::ir::ProgramUnit> prelude_units,
                       bool optimize = true);
Value InterpretProgram(const AST &ast);
Value InterpretSource(std::string_view source_text);

} // namespace compiler::interpreter
