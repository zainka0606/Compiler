#pragma once

#include "Parser.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace compiler::regex {

struct NFATransition {
    enum class Type {
        Epsilon,
        Literal,
        Dot,
        CharacterClass
    };

    Type type = Type::Epsilon;
    std::size_t target = 0;
    char literal = '\0';
    bool char_class_negated = false;
    std::vector<CharacterClassItem> char_class_items;

    static NFATransition Epsilon(std::size_t target);
    static NFATransition Literal(std::size_t target, char literal);
    static NFATransition Dot(std::size_t target);
    static NFATransition CharacterClass(std::size_t target, bool negated, std::vector<CharacterClassItem> items);
};

struct NFAState {
    std::vector<NFATransition> transitions;
};

struct NFA {
    std::vector<NFAState> states;
    std::size_t start_state = 0;
    std::size_t accept_state = 0;
};

NFA CompileToNFA(const RegexNode& node);
NFA CompilePatternToNFA(std::string_view pattern);
bool NFAMatches(const NFA& nfa, std::string_view input);

} // namespace compiler::regex
