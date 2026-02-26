#include "NFA.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace compiler::regex {
namespace {
struct Fragment {
    std::size_t start = 0;
    std::size_t end = 0;
};

class NFABuilder {
  public:
    Fragment Build(const RegexNode &node) {
        switch (node.type) {
        case RegexNode::Type::Empty:
            return BuildEmpty();
        case RegexNode::Type::Literal:
            return BuildLiteral(node.literal);
        case RegexNode::Type::Dot:
            return BuildDot();
        case RegexNode::Type::Sequence:
            return BuildSequence(node.children);
        case RegexNode::Type::Alternation:
            return BuildAlternation(node.children);
        case RegexNode::Type::Repetition:
            return BuildRepetition(node);
        case RegexNode::Type::Group:
            return BuildGroup(node);
        case RegexNode::Type::CharacterClass:
            return BuildCharacterClass(node.char_class_negated,
                                       node.char_class_items);
        }

        throw std::logic_error("unsupported regex node type");
    }

    [[nodiscard]] NFA Finish(Fragment root) && {
        NFA nfa;
        nfa.states = std::move(states_);
        nfa.start_state = root.start;
        nfa.accept_state = root.end;
        return nfa;
    }

  private:
    std::vector<NFAState> states_;

    std::size_t AddState() {
        states_.push_back(NFAState{});
        return states_.size() - 1;
    }

    void AddTransition(std::size_t from, NFATransition transition) {
        states_[from].transitions.push_back(std::move(transition));
    }

    void AddEpsilon(std::size_t from, std::size_t to) {
        AddTransition(from, NFATransition::Epsilon(to));
    }

    Fragment BuildEmpty() {
        const std::size_t start = AddState();
        const std::size_t end = AddState();
        AddEpsilon(start, end);
        return {start, end};
    }

    Fragment BuildLiteral(char c) {
        const std::size_t start = AddState();
        const std::size_t end = AddState();
        AddTransition(start, NFATransition::Literal(end, c));
        return {start, end};
    }

    Fragment BuildDot() {
        const std::size_t start = AddState();
        const std::size_t end = AddState();
        AddTransition(start, NFATransition::Dot(end));
        return {start, end};
    }

    Fragment BuildCharacterClass(bool negated,
                                 const std::vector<CharacterClassItem> &items) {
        const std::size_t start = AddState();
        const std::size_t end = AddState();
        AddTransition(start,
                      NFATransition::CharacterClass(end, negated, items));
        return {start, end};
    }

    Fragment BuildGroup(const RegexNode &node) {
        if (node.children.empty()) {
            return BuildEmpty();
        }
        return Build(node.children.front());
    }

    Fragment BuildSequence(const std::vector<RegexNode> &children) {
        if (children.empty()) {
            return BuildEmpty();
        }

        Fragment result = Build(children.front());
        for (std::size_t i = 1; i < children.size(); ++i) {
            Fragment next = Build(children[i]);
            AddEpsilon(result.end, next.start);
            result.end = next.end;
        }
        return result;
    }

    Fragment BuildAlternation(const std::vector<RegexNode> &branches) {
        if (branches.empty()) {
            return BuildEmpty();
        }

        const std::size_t start = AddState();
        const std::size_t end = AddState();

        for (const auto &branch : branches) {
            Fragment fragment = Build(branch);
            AddEpsilon(start, fragment.start);
            AddEpsilon(fragment.end, end);
        }

        return {start, end};
    }

    Fragment BuildRepetition(const RegexNode &node) {
        if (node.children.empty()) {
            return BuildEmpty();
        }
        return BuildRepetition(node.children.front(), node.repetition.min,
                               node.repetition.max);
    }

    Fragment BuildRepetition(const RegexNode &operand, std::size_t min,
                             const std::optional<std::size_t> &max) {
        bool has_result = false;
        Fragment result{};

        auto append = [&](Fragment fragment) {
            if (!has_result) {
                result = fragment;
                has_result = true;
                return;
            }

            AddEpsilon(result.end, fragment.start);
            result.end = fragment.end;
        };

        for (std::size_t i = 0; i < min; ++i) {
            append(Build(operand));
        }

        if (max.has_value()) {
            for (std::size_t i = min; i < *max; ++i) {
                append(BuildOptional(operand));
            }

            if (!has_result) {
                return BuildEmpty();
            }
            return result;
        }

        append(BuildKleeneStar(operand));
        return result;
    }

    Fragment BuildOptional(const RegexNode &operand) {
        const std::size_t start = AddState();
        const std::size_t end = AddState();
        Fragment body = Build(operand);

        AddEpsilon(start, end);
        AddEpsilon(start, body.start);
        AddEpsilon(body.end, end);
        return {start, end};
    }

    Fragment BuildKleeneStar(const RegexNode &operand) {
        const std::size_t start = AddState();
        const std::size_t end = AddState();
        Fragment body = Build(operand);

        AddEpsilon(start, end);
        AddEpsilon(start, body.start);
        AddEpsilon(body.end, body.start);
        AddEpsilon(body.end, end);
        return {start, end};
    }
};

bool MatchCharacterClass(const NFATransition &transition, unsigned char value) {
    bool matched = false;

    for (const auto &item : transition.char_class_items) {
        const unsigned char first = static_cast<unsigned char>(item.first);
        const unsigned char last = static_cast<unsigned char>(item.last);
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

bool TransitionMatches(const NFATransition &transition, char c) {
    const unsigned char value = static_cast<unsigned char>(c);

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

std::vector<std::size_t> EpsilonClosure(const NFA &nfa,
                                        const std::vector<std::size_t> &seeds) {
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

        for (const auto &transition : nfa.states[state].transitions) {
            if (transition.type == NFATransition::Type::Epsilon &&
                transition.target < nfa.states.size()) {
                stack.push_back(transition.target);
            }
        }
    }

    return closure;
}
} // namespace

NFATransition NFATransition::Epsilon(std::size_t target) {
    NFATransition transition;
    transition.type = Type::Epsilon;
    transition.target = target;
    return transition;
}

NFATransition NFATransition::Literal(std::size_t target, char literal) {
    NFATransition transition;
    transition.type = Type::Literal;
    transition.target = target;
    transition.literal = literal;
    return transition;
}

NFATransition NFATransition::Dot(std::size_t target) {
    NFATransition transition;
    transition.type = Type::Dot;
    transition.target = target;
    return transition;
}

NFATransition
NFATransition::CharacterClass(std::size_t target, bool negated,
                              std::vector<CharacterClassItem> items) {
    NFATransition transition;
    transition.type = Type::CharacterClass;
    transition.target = target;
    transition.char_class_negated = negated;
    transition.char_class_items = std::move(items);
    return transition;
}

NFA CompileToNFA(const RegexNode &node) {
    NFABuilder builder;
    Fragment root = builder.Build(node);
    return std::move(builder).Finish(root);
}

NFA CompilePatternToNFA(std::string_view pattern) {
    return CompileToNFA(Parse(pattern));
}

bool NFAMatches(const NFA &nfa, std::string_view input) {
    if (nfa.states.empty()) {
        return false;
    }
    if (nfa.start_state >= nfa.states.size() ||
        nfa.accept_state >= nfa.states.size()) {
        return false;
    }

    std::vector<std::size_t> current = EpsilonClosure(nfa, {nfa.start_state});

    for (char c : input) {
        std::vector<std::size_t> next_seeds;

        for (std::size_t state : current) {
            for (const auto &transition : nfa.states[state].transitions) {
                if (transition.type == NFATransition::Type::Epsilon) {
                    continue;
                }
                if (transition.target >= nfa.states.size()) {
                    continue;
                }
                if (TransitionMatches(transition, c)) {
                    next_seeds.push_back(transition.target);
                }
            }
        }

        current = EpsilonClosure(nfa, next_seeds);
        if (current.empty()) {
            return false;
        }
    }

    return std::find(current.begin(), current.end(), nfa.accept_state) !=
           current.end();
}
} // namespace compiler::regex