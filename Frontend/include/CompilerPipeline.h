#pragma once

#include "AST.h"
#include "IR.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace compiler::frontend_pipeline {

class FrontendPipelineException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

using AST = compiler::interpreter::AST;

AST ParseProgram(std::string_view source_text);
compiler::ir::Program CompileProgramToIR(const AST &ast);
std::vector<compiler::ir::ProgramUnit> CompileStandardLibraryToIR();

inline std::string
ASTToGraphvizDot(const AST &ast,
                 std::string_view graph_name = "program_ast") {
    return generated::Neon::NeonParser::ASTToGraphvizDot(ast, graph_name);
}

} // namespace compiler::frontend_pipeline
