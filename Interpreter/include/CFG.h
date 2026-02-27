#pragma once

#include "AST.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace compiler::interpreter {

struct CFGNode {
    std::size_t id = 0;
    std::string label;
};

struct CFGEdge {
    std::size_t from = 0;
    std::size_t to = 0;
    std::string label;
};

struct CFGGraph {
    std::string name;
    std::size_t entry = 0;
    std::size_t exit = 0;
    std::vector<CFGNode> nodes;
    std::vector<CFGEdge> edges;
};

struct ProgramCFG {
    CFGGraph top_level;
    std::vector<CFGGraph> functions;
};

ProgramCFG BuildProgramCFG(const AST &ast);
std::string
ProgramCFGToGraphvizDot(const ProgramCFG &cfg,
                        std::string_view graph_name = "program_cfg");

} // namespace compiler::interpreter
