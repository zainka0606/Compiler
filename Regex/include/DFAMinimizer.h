#pragma once

#include "DFA.h"

#include <string_view>

namespace compiler::regex {

DFA MinimizeDFA(const DFA& dfa);
DFA CompilePatternToMinimizedDFA(std::string_view pattern);

} // namespace compiler::regex
