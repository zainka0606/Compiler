#pragma once

#include "NFA.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace compiler::regex {
inline constexpr std::size_t kInvalidDFAState = static_cast<std::size_t>(-1);

struct DFAState {
    std::array<std::size_t, 256> transitions{};
    bool is_accepting = false;
};

struct DFA {
    std::vector<DFAState> states;
    std::size_t start_state = kInvalidDFAState;
};

DFA CompileNFAToDFA(const NFA &nfa);
DFA CompilePatternToDFA(std::string_view pattern);
bool DFAMatches(const DFA &dfa, std::string_view input);
} // namespace compiler::regex