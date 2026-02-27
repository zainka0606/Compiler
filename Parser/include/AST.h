#pragma once

#include "GeneratedParser.h"

#include <string>
#include <string_view>

namespace compiler::lang {

using AST = generated::MiniLang::MiniLangParser::AST;

inline std::string
ASTToGraphvizDot(const AST &ast, std::string_view graph_name = "program_ast") {
    return generated::MiniLang::MiniLangParser::ASTToGraphvizDot(ast,
                                                                 graph_name);
}

} // namespace compiler::lang
