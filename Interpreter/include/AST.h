#pragma once

#include "GeneratedParser.h"

#include <string>
#include <string_view>

namespace compiler::interpreter {

using AST = generated::MiniLangInterpreter::MiniLangInterpreterParser::AST;

inline std::string
ASTToGraphvizDot(const AST &ast, std::string_view graph_name = "program_ast") {
    return generated::MiniLangInterpreter::MiniLangInterpreterParser::
        ASTToGraphvizDot(ast, graph_name);
}

} // namespace compiler::interpreter
