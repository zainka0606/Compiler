#include "DFA.h"

#include <algorithm>
#include <map>
#include <queue>
#include <utility>

namespace compiler::regex {

namespace {

bool MatchCharacterClass(const NFATransition& transition, unsigned char value) {
    bool matched = false;

    for (const auto& item : transition.char_class_items) {
        const auto first = static_cast<unsigned char>(item.first);
        const auto last = static_cast<unsigned char>(item.last);

        if (item.is_range) {
            if (first <= value && value <= last) {
                matched = true;
                break;
            }
        } else if (first == value) {
            matched = true;
            break;
        }
    }

    return transition.char_class_negated ? !matched : matched;
}

bool TransitionMatchesByte(const NFATransition& transition, unsigned char value) {
    switch (transition.type) {
        case NFATransition::Type::Epsilon:
            return false;
        case NFATransition::Type::Literal:
            return value == static_cast<unsigned char>(transition.literal);
        case NFATransition::Type::Dot:
            return true;
        case NFATransition::Type::CharacterClass:
            return MatchCharacterClass(transition, value);
    }

    return false;
}

std::vector<std::size_t> EpsilonClosure(const NFA& nfa, const std::vector<std::size_t>& seeds) {
    std::vector<std::size_t> closure;
    closure.reserve(nfa.states.size());

    std::vector<bool> visited(nfa.states.size(), false);
    std::vector<std::size_t> stack;
    stack.reserve(seeds.size());

    for (std::size_t state : seeds) {
        if (state < nfa.states.size()) {
            stack.push_back(state);
        }
    }

    while (!stack.empty()) {
        const std::size_t state = stack.back();
        stack.pop_back();

        if (visited[state]) {
            continue;
        }

        visited[state] = true;
        closure.push_back(state);

        for (const auto& transition : nfa.states[state].transitions) {
            if (transition.type == NFATransition::Type::Epsilon && transition.target < nfa.states.size()) {
                stack.push_back(transition.target);
            }
        }
    }

    std::sort(closure.begin(), closure.end());
    closure.erase(std::unique(closure.begin(), closure.end()), closure.end());
    return closure;
}

std::vector<std::size_t> MoveOnByte(const NFA& nfa,
                                    const std::vector<std::size_t>& states,
                                    unsigned char value) {
    std::vector<std::size_t> moved;

    for (std::size_t state : states) {
        if (state >= nfa.states.size()) {
            continue;
        }

        for (const auto& transition : nfa.states[state].transitions) {
            if (transition.target >= nfa.states.size()) {
                continue;
            }
            if (transition.type == NFATransition::Type::Epsilon) {
                continue;
            }
            if (TransitionMatchesByte(transition, value)) {
                moved.push_back(transition.target);
            }
        }
    }

    std::sort(moved.begin(), moved.end());
    moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
    return moved;
}

bool ContainsState(const std::vector<std::size_t>& subset, std::size_t state) {
    return std::binary_search(subset.begin(), subset.end(), state);
}

DFAState MakeDFAState(bool is_accepting) {
    DFAState state;
    state.transitions.fill(kInvalidDFAState);
    state.is_accepting = is_accepting;
    return state;
}

} // namespace

DFA CompileNFAToDFA(const NFA& nfa) {
    DFA dfa;

    if (nfa.states.empty()) {
        return dfa;
    }
    if (nfa.start_state >= nfa.states.size() || nfa.accept_state >= nfa.states.size()) {
        return dfa;
    }

    const std::vector<std::size_t> start_subset = EpsilonClosure(nfa, {nfa.start_state});
    if (start_subset.empty()) {
        return dfa;
    }

    std::map<std::vector<std::size_t>, std::size_t> subset_to_index;
    std::queue<std::vector<std::size_t>> pending;

    constexpr std::size_t start_index = 0;
    subset_to_index.emplace(start_subset, start_index);
    dfa.states.push_back(MakeDFAState(ContainsState(start_subset, nfa.accept_state)));
    dfa.start_state = start_index;
    pending.push(start_subset);

    while (!pending.empty()) {
        const std::vector<std::size_t> subset = pending.front();
        pending.pop();

        const std::size_t dfa_index = subset_to_index.at(subset);

        for (std::size_t byte_value = 0; byte_value < 256; ++byte_value) {
            const auto moved = MoveOnByte(nfa, subset, static_cast<unsigned char>(byte_value));
            if (moved.empty()) {
                continue;
            }

            const auto next_subset = EpsilonClosure(nfa, moved);
            if (next_subset.empty()) {
                continue;
            }

            auto [it, inserted] = subset_to_index.emplace(next_subset, dfa.states.size());
            if (inserted) {
                dfa.states.push_back(MakeDFAState(ContainsState(next_subset, nfa.accept_state)));
                pending.push(next_subset);
            }

            dfa.states[dfa_index].transitions[byte_value] = it->second;
        }
    }

    return dfa;
}

DFA CompilePatternToDFA(std::string_view pattern) {
    return CompileNFAToDFA(CompilePatternToNFA(pattern));
}

bool DFAMatches(const DFA& dfa, std::string_view input) {
    if (dfa.start_state == kInvalidDFAState || dfa.start_state >= dfa.states.size()) {
        return false;
    }

    std::size_t state = dfa.start_state;
    for (const char c : input) {
        const auto byte = static_cast<unsigned char>(c);
        const std::size_t next = dfa.states[state].transitions[byte];
        if (next == kInvalidDFAState || next >= dfa.states.size()) {
            return false;
        }
        state = next;
    }

    return dfa.states[state].is_accepting;
}

} // namespace compiler::regex
