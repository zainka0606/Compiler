#include "Generator.h"
#include "Common/Graphviz.h"
#include "Common/Identifier.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace compiler::lexgen {
namespace {
std::string QuoteForMessage(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    out += "\"";
    return out;
}

std::string EscapeRegexLiteralChar(char c) {
    switch (c) {
    case '\\':
    case '.':
    case '[':
    case ']':
    case '(':
    case ')':
    case '{':
    case '}':
    case '*':
    case '+':
    case '?':
    case '|':
        return std::string("\\") + c;
    case '\n':
        return "\\n";
    case '\r':
        return "\\r";
    case '\t':
        return "\\t";
    default:
        return std::string(1, c);
    }
}

std::string EscapeRegexFromLiteral(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (char c : text) {
        out += EscapeRegexLiteralChar(c);
    }
    return out;
}

std::string EscapeDebugChar(char c) {
    switch (c) {
    case '\n':
        return "\\n";
    case '\r':
        return "\\r";
    case '\t':
        return "\\t";
    case '\\':
        return "\\\\";
    case '"':
        return "\\\"";
    default:
        return std::string(1, c);
    }
}

std::string OpenNamespaces(const std::vector<std::string> &parts) {
    std::ostringstream oss;
    for (const auto &part : parts) {
        oss << "namespace " << part << " {\n";
    }
    if (!parts.empty()) {
        oss << "\n";
    }
    return oss.str();
}

std::string CloseNamespaces(const std::vector<std::string> &parts) {
    std::ostringstream oss;
    if (!parts.empty()) {
        oss << "\n";
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto &part = parts[parts.size() - 1 - i];
        oss << "} // namespace " << part << "\n";
    }
    return oss.str();
}

std::string PatternSourceText(const PatternSource &pattern) {
    return pattern.kind == PatternSource::Kind::Regex
               ? ("/" + pattern.text + "/")
               : QuoteForMessage(pattern.text);
}

std::string ExpandRegexTemplate(
    const std::string &raw_regex,
    const std::function<std::string(const std::string &)> &resolve_macro) {
    std::string out;
    out.reserve(raw_regex.size() + 16);

    for (std::size_t i = 0; i < raw_regex.size();) {
        const char c = raw_regex[i];

        if (c == '\\') {
            if ((i + 1) >= raw_regex.size()) {
                out.push_back(c);
                ++i;
            } else {
                out.push_back(raw_regex[i]);
                out.push_back(raw_regex[i + 1]);
                i += 2;
            }
            continue;
        }

        if (c == '{' && (i + 1) < raw_regex.size() && raw_regex[i + 1] == '{') {
            const std::size_t name_start = i + 2;
            const std::size_t close = raw_regex.find("}}", name_start);
            if (close == std::string::npos) {
                throw LexerCompileException(
                    "unterminated macro placeholder in regex literal: " +
                    raw_regex);
            }

            const std::string name =
                raw_regex.substr(name_start, close - name_start);
            if (name.empty()) {
                throw LexerCompileException(
                    "empty macro placeholder in regex literal: " + raw_regex);
            }

            out += "(" + resolve_macro(name) + ")";
            i = close + 2;
            continue;
        }

        out.push_back(c);
        ++i;
    }

    return out;
}

std::string TransitionLabel(const regex::NFATransition &transition) {
    switch (transition.type) {
    case regex::NFATransition::Type::Epsilon:
        return "eps";
    case regex::NFATransition::Type::Literal:
        return "'" + EscapeDebugChar(transition.literal) + "'";
    case regex::NFATransition::Type::Dot:
        return ".";
    case regex::NFATransition::Type::CharacterClass: {
        std::string label = "[";
        if (transition.char_class_negated) {
            label += "^";
        }
        for (const auto &item : transition.char_class_items) {
            if (item.is_range) {
                label += EscapeDebugChar(item.first);
                label += "-";
                label += EscapeDebugChar(item.last);
            } else {
                label += EscapeDebugChar(item.first);
            }
        }
        label += "]";
        return label;
    }
    }
    return "?";
}

bool MatchCharacterClass(const regex::NFATransition &transition,
                         unsigned char value) {
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

bool TransitionMatchesByte(const regex::NFATransition &transition,
                           unsigned char value) {
    switch (transition.type) {
    case regex::NFATransition::Type::Epsilon:
        return false;
    case regex::NFATransition::Type::Literal:
        return value == static_cast<unsigned char>(transition.literal);
    case regex::NFATransition::Type::Dot:
        return true;
    case regex::NFATransition::Type::CharacterClass:
        return MatchCharacterClass(transition, value);
    }

    return false;
}

std::vector<std::size_t>
EpsilonClosureSorted(const regex::NFA &nfa,
                     const std::vector<std::size_t> &seeds) {
    std::vector<std::size_t> closure;
    closure.reserve(nfa.states.size());

    std::vector<bool> visited(nfa.states.size(), false);
    std::vector<std::size_t> stack;
    stack.reserve(seeds.size());

    for (std::size_t seed : seeds) {
        if (seed < nfa.states.size()) {
            stack.push_back(seed);
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
            if (transition.type == regex::NFATransition::Type::Epsilon &&
                transition.target < nfa.states.size()) {
                stack.push_back(transition.target);
            }
        }
    }

    std::sort(closure.begin(), closure.end());
    closure.erase(std::unique(closure.begin(), closure.end()), closure.end());
    return closure;
}

std::vector<std::size_t> MoveOnByte(const regex::NFA &nfa,
                                    const std::vector<std::size_t> &states,
                                    unsigned char value) {
    std::vector<std::size_t> moved;

    for (std::size_t state : states) {
        if (state >= nfa.states.size()) {
            continue;
        }

        for (const auto &transition : nfa.states[state].transitions) {
            if (transition.type == regex::NFATransition::Type::Epsilon) {
                continue;
            }
            if (transition.target >= nfa.states.size()) {
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

struct CombinedNFABuild {
    regex::NFA nfa;
    std::vector<std::size_t> accept_rule_by_state;
    std::vector<std::size_t> owner_rule_by_state;
};

CombinedNFABuild BuildCombinedNFA(const std::vector<CompiledRule> &rules) {
    CombinedNFABuild result;
    result.nfa.states.resize(1); // super-start
    result.nfa.start_state = 0;
    result.nfa.accept_state = 0; // unused for multi-rule determinization
    result.accept_rule_by_state.resize(1, kInvalidAcceptRuleIndex);
    result.owner_rule_by_state.resize(1, kInvalidAcceptRuleIndex);

    for (const auto &rule : rules) {
        if (rule.nfa.states.empty()) {
            throw LexerCompileException("internal error: rule '" + rule.name +
                                        "' has empty NFA");
        }

        const std::size_t offset = result.nfa.states.size();
        result.nfa.states.insert(result.nfa.states.end(),
                                 rule.nfa.states.begin(),
                                 rule.nfa.states.end());
        result.accept_rule_by_state.resize(result.nfa.states.size(),
                                           kInvalidAcceptRuleIndex);
        result.owner_rule_by_state.resize(result.nfa.states.size(),
                                          kInvalidAcceptRuleIndex);
        for (std::size_t local_index = 0; local_index < rule.nfa.states.size();
             ++local_index) {
            result.owner_rule_by_state[offset + local_index] = rule.rule_index;
        }

        for (std::size_t local_index = 0; local_index < rule.nfa.states.size();
             ++local_index) {
            auto &state = result.nfa.states[offset + local_index];
            for (auto &transition : state.transitions) {
                transition.target += offset;
            }
        }

        result.nfa.states[result.nfa.start_state].transitions.push_back(
            regex::NFATransition::Epsilon(offset + rule.nfa.start_state));
        result.accept_rule_by_state[offset + rule.nfa.accept_state] =
            rule.rule_index;
    }

    return result;
}

std::size_t LowestAcceptingRuleInSubset(
    const std::vector<std::size_t> &subset,
    const std::vector<std::size_t> &accept_rule_by_state) {
    std::size_t best = kInvalidAcceptRuleIndex;
    for (std::size_t state : subset) {
        if (state >= accept_rule_by_state.size()) {
            continue;
        }
        const std::size_t rule_index = accept_rule_by_state[state];
        if (rule_index == kInvalidAcceptRuleIndex) {
            continue;
        }
        if (best == kInvalidAcceptRuleIndex || rule_index < best) {
            best = rule_index;
        }
    }
    return best;
}

std::size_t
ExclusiveRuleInSubset(const std::vector<std::size_t> &subset,
                      const std::vector<std::size_t> &owner_rule_by_state) {
    std::size_t only_rule = kInvalidAcceptRuleIndex;
    for (std::size_t state : subset) {
        if (state >= owner_rule_by_state.size()) {
            continue;
        }
        const std::size_t rule_index = owner_rule_by_state[state];
        if (rule_index == kInvalidAcceptRuleIndex) {
            continue;
        }
        if (only_rule == kInvalidAcceptRuleIndex) {
            only_rule = rule_index;
            continue;
        }
        if (only_rule != rule_index) {
            return kInvalidAcceptRuleIndex;
        }
    }
    return only_rule;
}

CombinedDFA BuildCombinedDFA(const std::vector<CompiledRule> &rules) {
    const CombinedNFABuild combined_nfa = BuildCombinedNFA(rules);
    const regex::NFA &nfa = combined_nfa.nfa;
    const auto &accept_rule_by_state = combined_nfa.accept_rule_by_state;
    const auto &owner_rule_by_state = combined_nfa.owner_rule_by_state;

    CombinedDFA dfa;
    if (nfa.states.empty() || nfa.start_state >= nfa.states.size()) {
        return dfa;
    }

    const std::vector<std::size_t> start_subset =
        EpsilonClosureSorted(nfa, {nfa.start_state});
    if (start_subset.empty()) {
        return dfa;
    }

    std::map<std::vector<std::size_t>, std::size_t> subset_to_index;
    std::queue<std::vector<std::size_t>> pending;

    subset_to_index.emplace(start_subset, 0);
    CombinedDFAState start_state{};
    start_state.transitions.fill(regex::kInvalidDFAState);
    start_state.accepting_rule_index =
        LowestAcceptingRuleInSubset(start_subset, accept_rule_by_state);
    start_state.exclusive_rule_index =
        ExclusiveRuleInSubset(start_subset, owner_rule_by_state);
    dfa.states.push_back(start_state);
    dfa.start_state = 0;
    pending.push(start_subset);

    while (!pending.empty()) {
        const std::vector<std::size_t> subset = pending.front();
        pending.pop();

        const std::size_t dfa_state_index = subset_to_index.at(subset);
        for (std::size_t byte = 0; byte < 256; ++byte) {
            const auto moved =
                MoveOnByte(nfa, subset, static_cast<unsigned char>(byte));
            if (moved.empty()) {
                continue;
            }
            const auto next_subset = EpsilonClosureSorted(nfa, moved);
            if (next_subset.empty()) {
                continue;
            }

            auto [it, inserted] =
                subset_to_index.emplace(next_subset, dfa.states.size());
            if (inserted) {
                CombinedDFAState new_state{};
                new_state.transitions.fill(regex::kInvalidDFAState);
                new_state.accepting_rule_index = LowestAcceptingRuleInSubset(
                    next_subset, accept_rule_by_state);
                new_state.exclusive_rule_index =
                    ExclusiveRuleInSubset(next_subset, owner_rule_by_state);
                dfa.states.push_back(std::move(new_state));
                pending.push(next_subset);
            }

            dfa.states[dfa_state_index].transitions[byte] = it->second;
        }
    }

    return dfa;
}

std::string DescribeByte(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    std::ostringstream oss;
    oss << "'" << EscapeDebugChar(c) << "' (0x" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0') << static_cast<int>(u) << ")";
    return oss.str();
}

void AdvanceLocation(std::string_view text, std::size_t &line,
                     std::size_t &column) {
    for (char c : text) {
        if (c == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
}
} // namespace

LexerCompileException::LexerCompileException(std::string message)
    : std::runtime_error(std::move(message)) {}

LexerRuntimeException::LexerRuntimeException(std::size_t offset,
                                             std::size_t line,
                                             std::size_t column,
                                             std::string message)
    : std::runtime_error(std::move(message)), offset_(offset), line_(line),
      column_(column) {}

std::size_t LexerRuntimeException::offset() const noexcept { return offset_; }

std::size_t LexerRuntimeException::line() const noexcept { return line_; }

std::size_t LexerRuntimeException::column() const noexcept { return column_; }

CompiledLexer CompileLexerSpec(const LexerSpecAST &spec) {
    if (spec.rules.empty()) {
        throw LexerCompileException("lexer spec must define at least one rule");
    }

    std::unordered_map<std::string, const MacroDefinition *> macros;
    for (const auto &macro : spec.macros) {
        if (!macros.emplace(macro.name, &macro).second) {
            throw LexerCompileException("duplicate macro definition: " +
                                        macro.name);
        }
    }

    {
        std::unordered_set<std::string> names;
        for (const auto &rule : spec.rules) {
            if (!names.insert(rule.name).second) {
                throw LexerCompileException("duplicate rule definition: " +
                                            rule.name);
            }
        }
    }

    enum class VisitState { Unvisited, Visiting, Done };
    std::unordered_map<std::string, VisitState> macro_state;
    std::unordered_map<std::string, std::string> expanded_macros;
    for (const auto &macro : spec.macros) {
        macro_state.emplace(macro.name, VisitState::Unvisited);
    }

    std::function<std::string(const PatternSource &)> expand_pattern;
    std::function<std::string(const std::string &)> expand_macro;

    expand_pattern = [&](const PatternSource &pattern) -> std::string {
        if (pattern.kind == PatternSource::Kind::StringLiteral) {
            return EscapeRegexFromLiteral(pattern.text);
        }
        return ExpandRegexTemplate(pattern.text, expand_macro);
    };

    expand_macro = [&](const std::string &name) -> std::string {
        const auto macro_it = macros.find(name);
        if (macro_it == macros.end()) {
            throw LexerCompileException("unknown macro referenced in regex: " +
                                        name);
        }

        auto state_it = macro_state.find(name);
        if (state_it == macro_state.end()) {
            throw LexerCompileException("internal macro state error for: " +
                                        name);
        }
        if (state_it->second == VisitState::Done) {
            return expanded_macros.at(name);
        }
        if (state_it->second == VisitState::Visiting) {
            throw LexerCompileException(
                "recursive macro expansion detected for: " + name);
        }

        state_it->second = VisitState::Visiting;
        std::string expanded = expand_pattern(macro_it->second->pattern);
        state_it->second = VisitState::Done;
        expanded_macros[name] = expanded;
        return expanded;
    };

    for (const auto &macro : spec.macros) {
        (void)expand_macro(macro.name);
    }

    CompiledLexer compiled;
    compiled.spec = spec;
    compiled.rules.reserve(spec.rules.size());

    for (std::size_t i = 0; i < spec.rules.size(); ++i) {
        const auto &rule = spec.rules[i];

        CompiledRule out_rule;
        out_rule.name = rule.name;
        out_rule.skip = rule.skip;
        out_rule.rule_index = i;
        out_rule.source_pattern = rule.pattern.text;
        out_rule.expanded_regex_pattern = expand_pattern(rule.pattern);

        try {
            out_rule.regex_ast = regex::Parse(out_rule.expanded_regex_pattern);
        } catch (const regex::ParseException &ex) {
            std::ostringstream oss;
            oss << "regex parse failed for rule '" << rule.name
                << "' at regex offset " << ex.position() << ": " << ex.what()
                << " (expanded pattern "
                << QuoteForMessage(out_rule.expanded_regex_pattern) << ")";
            throw LexerCompileException(oss.str());
        }

        out_rule.nfa = regex::CompileToNFA(out_rule.regex_ast);
        out_rule.dfa = regex::CompileNFAToDFA(out_rule.nfa);
        out_rule.minimized_dfa = regex::MinimizeDFA(out_rule.dfa);

        if (regex::DFAMatches(out_rule.minimized_dfa, "")) {
            throw LexerCompileException("rule '" + rule.name +
                                        "' can match the empty string");
        }

        compiled.rules.push_back(std::move(out_rule));
    }

    compiled.combined_dfa = BuildCombinedDFA(compiled.rules);
    if (compiled.combined_dfa.start_state == regex::kInvalidDFAState ||
        compiled.combined_dfa.states.empty()) {
        throw LexerCompileException("failed to build combined lexer DFA");
    }

    return compiled;
}

CompiledLexer CompileLexerSpec(std::string_view source_text) {
    return CompileLexerSpec(ParseLexerSpec(source_text));
}

std::vector<RuntimeToken> Tokenize(const CompiledLexer &lexer,
                                   std::string_view input) {
    std::vector<RuntimeToken> tokens;
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;

    while (offset < input.size()) {
        const auto &dfa = lexer.combined_dfa;
        if (dfa.start_state == regex::kInvalidDFAState ||
            dfa.start_state >= dfa.states.size()) {
            throw LexerRuntimeException(offset, line, column,
                                        "combined lexer DFA is invalid");
        }

        std::size_t state = dfa.start_state;
        std::size_t best_rule_index = kInvalidAcceptRuleIndex;
        std::optional<std::size_t> best_len;

        if (dfa.states[state].accepting_rule_index != kInvalidAcceptRuleIndex) {
            best_rule_index = dfa.states[state].accepting_rule_index;
            best_len = 0;
        }

        for (std::size_t i = 0; (offset + i) < input.size(); ++i) {
            const unsigned char byte =
                static_cast<unsigned char>(input[offset + i]);
            const std::size_t next = dfa.states[state].transitions[byte];
            if (next == regex::kInvalidDFAState || next >= dfa.states.size()) {
                break;
            }
            state = next;

            const std::size_t accept_rule =
                dfa.states[state].accepting_rule_index;
            if (accept_rule != kInvalidAcceptRuleIndex) {
                best_rule_index = accept_rule;
                best_len = i + 1;
            }
        }

        if (best_rule_index == kInvalidAcceptRuleIndex ||
            !best_len.has_value() || *best_len == 0) {
            std::ostringstream oss;
            oss << "no lexer rule matched input at " << line << ":" << column;
            if (offset < input.size()) {
                oss << " near " << DescribeByte(input[offset]);
            }
            throw LexerRuntimeException(offset, line, column, oss.str());
        }

        if (best_rule_index >= lexer.rules.size()) {
            throw LexerRuntimeException(
                offset, line, column,
                "combined lexer DFA returned out-of-range rule index");
        }

        const auto &best_rule = lexer.rules[best_rule_index];
        const std::string_view lexeme = input.substr(offset, *best_len);
        if (!best_rule.skip) {
            tokens.push_back(RuntimeToken{best_rule.name, std::string(lexeme),
                                          offset, line, column});
        }

        AdvanceLocation(lexeme, line, column);
        offset += *best_len;
    }

    tokens.push_back(RuntimeToken{"EndOfFile", "", offset, line, column});
    return tokens;
}

std::string BuildCompiledASTDot(const CompiledLexer &lexer) {
    std::ostringstream oss;
    oss << "digraph CompiledAST {\n";
    oss << "  rankdir=TB;\n";
    oss << "  node [shape=box];\n";

    std::size_t next_node_id = 0;
    auto node_id = [&](std::size_t id) { return "n" + std::to_string(id); };
    auto add_node = [&](std::string_view label) -> std::size_t {
        const std::size_t id = next_node_id++;
        oss << "  " << node_id(id) << " [label=\""
            << compiler::common::EscapeGraphvizLabel(label) << "\"];\n";
        return id;
    };
    auto add_edge = [&](std::size_t from, std::size_t to,
                        std::string_view label = {}) {
        oss << "  " << node_id(from) << " -> " << node_id(to);
        if (!label.empty()) {
            oss << " [label=\"" << compiler::common::EscapeGraphvizLabel(label)
                << "\"]";
        }
        oss << ";\n";
    };
    auto make_label = [](std::string_view name,
                         std::initializer_list<std::string> properties = {}) {
        std::string label(name);
        for (const auto &property : properties) {
            if (!property.empty()) {
                label += "\n";
                label += property;
            }
        }
        return label;
    };

    auto format_char_class_items =
        [](const std::vector<regex::CharacterClassItem> &items) {
            std::string text;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) {
                    text += ", ";
                }
                const auto &item = items[i];
                if (item.is_range) {
                    text += EscapeDebugChar(item.first);
                    text += "-";
                    text += EscapeDebugChar(item.last);
                } else {
                    text += EscapeDebugChar(item.first);
                }
            }
            return text;
        };

    std::function<std::size_t(const regex::RegexNode &)> add_regex_ast =
        [&](const regex::RegexNode &node) -> std::size_t {
        std::string label;
        switch (node.type) {
        case regex::RegexNode::Type::Empty:
            label = make_label("Empty");
            break;
        case regex::RegexNode::Type::Literal:
            label = make_label(
                "Literal", {"value='" + EscapeDebugChar(node.literal) + "'"});
            break;
        case regex::RegexNode::Type::Dot:
            label = make_label("Dot");
            break;
        case regex::RegexNode::Type::Sequence:
            label = make_label("Sequence");
            break;
        case regex::RegexNode::Type::Alternation:
            label = make_label("Alternation");
            break;
        case regex::RegexNode::Type::Repetition:
            label =
                make_label("Repetition",
                           {"min=" + std::to_string(node.repetition.min),
                            "max=" + (node.repetition.max.has_value()
                                          ? std::to_string(*node.repetition.max)
                                          : "inf")});
            break;
        case regex::RegexNode::Type::Group:
            label = make_label("Group");
            break;
        case regex::RegexNode::Type::CharacterClass:
            label = make_label(
                "CharacterClass",
                {"negated=" +
                     std::string(node.char_class_negated ? "true" : "false"),
                 "items=[" + format_char_class_items(node.char_class_items) +
                     "]"});
            break;
        }

        const std::size_t id = add_node(label);
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const std::size_t child_id = add_regex_ast(node.children[i]);
            add_edge(id, child_id);
        }
        return id;
    };

    const std::size_t root_id = add_node("CompiledLexer");
    const std::size_t spec_id = add_node("LexerSpecAST");
    add_edge(root_id, spec_id);

    add_edge(spec_id,
             add_node(make_label("Lexer", {"name=" + lexer.spec.lexer_name})));
    add_edge(spec_id,
             add_node(make_label("TokenEnum",
                                 {"name=" + lexer.spec.token_enum_name})));

    const std::size_t namespace_id = add_node("Namespace");
    add_edge(spec_id, namespace_id);
    if (lexer.spec.namespace_parts.empty()) {
        add_edge(namespace_id, add_node(make_label("GlobalNamespace")));
    } else {
        for (const auto &part : lexer.spec.namespace_parts) {
            add_edge(namespace_id,
                     add_node(make_label("NamespacePart", {"name=" + part})));
        }
    }

    const std::size_t macros_id = add_node("Macros");
    add_edge(spec_id, macros_id);
    for (std::size_t i = 0; i < lexer.spec.macros.size(); ++i) {
        const auto &macro = lexer.spec.macros[i];
        const std::size_t macro_id = add_node(make_label(
            "Macro", {"name=" + macro.name, "index=" + std::to_string(i)}));
        add_edge(macros_id, macro_id);
        add_edge(macro_id,
                 add_node(make_label(
                     "Pattern",
                     {"kind=" + std::string(macro.pattern.kind ==
                                                    PatternSource::Kind::Regex
                                                ? "regex"
                                                : "string"),
                      "text=" + PatternSourceText(macro.pattern)})));
    }

    const std::size_t rules_id = add_node("Rules");
    add_edge(root_id, rules_id);
    for (const auto &rule : lexer.rules) {
        const auto &source_rule = lexer.spec.rules[rule.rule_index];
        const std::size_t rule_id = add_node(make_label(
            "Rule",
            {"index=" + std::to_string(rule.rule_index), "name=" + rule.name,
             "skip=" + std::string(rule.skip ? "true" : "false")}));
        add_edge(rules_id, rule_id);

        add_edge(rule_id,
                 add_node(make_label(
                     "Pattern",
                     {"kind=" + std::string(source_rule.pattern.kind ==
                                                    PatternSource::Kind::Regex
                                                ? "regex"
                                                : "string"),
                      "text=" + PatternSourceText(source_rule.pattern)})));

        const std::size_t regex_ast_id = add_node("Regex AST");
        add_edge(rule_id, regex_ast_id);
        const std::size_t regex_ast_root_id = add_regex_ast(rule.regex_ast);
        add_edge(regex_ast_id, regex_ast_root_id);
    }

    oss << "}\n";
    return oss.str();
}

std::string NFAToGraphvizDot(const regex::NFA &nfa,
                             std::string_view graph_name) {
    std::ostringstream oss;
    oss << "digraph " << compiler::common::SanitizeIdentifier(graph_name, "nfa") << " {\n";
    oss << "  rankdir=LR;\n";
    oss << "  __start [shape=point];\n";
    if (nfa.start_state < nfa.states.size()) {
        oss << "  __start -> s" << nfa.start_state << ";\n";
    }

    for (std::size_t i = 0; i < nfa.states.size(); ++i) {
        oss << "  s" << i << " [shape="
            << ((i == nfa.accept_state) ? "doublecircle" : "circle")
            << ", label=\"s" << i << "\"];\n";
    }

    for (std::size_t i = 0; i < nfa.states.size(); ++i) {
        for (const auto &transition : nfa.states[i].transitions) {
            oss << "  s" << i << " -> s" << transition.target << " [label=\""
                << compiler::common::EscapeGraphvizLabel(
                       TransitionLabel(transition))
                << "\"];\n";
        }
    }

    oss << "}\n";
    return oss.str();
}

std::string DFAToGraphvizDot(const regex::DFA &dfa,
                             std::string_view graph_name) {
    std::ostringstream oss;
    oss << "digraph " << compiler::common::SanitizeIdentifier(graph_name, "dfa") << " {\n";
    oss << "  rankdir=LR;\n";
    oss << "  __start [shape=point];\n";
    if (dfa.start_state != regex::kInvalidDFAState &&
        dfa.start_state < dfa.states.size()) {
        oss << "  __start -> s" << dfa.start_state << ";\n";
    }

    for (std::size_t i = 0; i < dfa.states.size(); ++i) {
        oss << "  s" << i << " [shape="
            << (dfa.states[i].is_accepting ? "doublecircle" : "circle")
            << ", label=\"s" << i << "\"];\n";
    }

    for (std::size_t i = 0; i < dfa.states.size(); ++i) {
        std::unordered_map<std::size_t, std::vector<unsigned>> groups;
        for (unsigned byte = 0; byte < 256; ++byte) {
            const std::size_t target = dfa.states[i].transitions[byte];
            if (target == regex::kInvalidDFAState ||
                target >= dfa.states.size()) {
                continue;
            }
            groups[target].push_back(byte);
        }

        for (const auto &[target, bytes] : groups) {
            std::string label;
            for (std::size_t j = 0; j < bytes.size(); ++j) {
                if (j > 0) {
                    label += ",";
                }
                label += EscapeDebugChar(static_cast<char>(bytes[j]));
            }
            oss << "  s" << i << " -> s" << target << " [label=\""
                << compiler::common::EscapeGraphvizLabel(label) << "\"];\n";
        }
    }

    oss << "}\n";
    return oss.str();
}

std::string CombinedDFAToGraphvizDot(const CompiledLexer &lexer,
                                     std::string_view graph_name) {
    const auto &dfa = lexer.combined_dfa;

    std::ostringstream oss;
    oss << "digraph " << compiler::common::SanitizeIdentifier(graph_name, "lexer_dfa") << " {\n";
    oss << "  rankdir=LR;\n";
    oss << "  __start [shape=point];\n";
    if (dfa.start_state != regex::kInvalidDFAState &&
        dfa.start_state < dfa.states.size()) {
        oss << "  __start -> s" << dfa.start_state << ";\n";
    }

    auto emit_state_node = [&](std::size_t i, std::string_view indent) {
        const bool accepting =
            dfa.states[i].accepting_rule_index != kInvalidAcceptRuleIndex;
        std::string label = "s" + std::to_string(i);
        if (accepting &&
            dfa.states[i].accepting_rule_index < lexer.rules.size()) {
            label += "\n";
            label += lexer.rules[dfa.states[i].accepting_rule_index].skip
                         ? "skip "
                         : "token ";
            label += lexer.rules[dfa.states[i].accepting_rule_index].name;
        }
        if (!accepting &&
            dfa.states[i].exclusive_rule_index == kInvalidAcceptRuleIndex) {
            label += "\nshared";
        }
        oss << indent << "s" << i
            << " [shape=" << (accepting ? "doublecircle" : "circle")
            << ", label=\"" << compiler::common::EscapeGraphvizLabel(label)
            << "\"];\n";
    };

    std::vector<bool> emitted_in_cluster(dfa.states.size(), false);
    for (std::size_t rule_index = 0; rule_index < lexer.rules.size();
         ++rule_index) {
        std::vector<std::size_t> cluster_states;
        for (std::size_t state_index = 0; state_index < dfa.states.size();
             ++state_index) {
            if (dfa.states[state_index].exclusive_rule_index == rule_index) {
                cluster_states.push_back(state_index);
            }
        }

        if (cluster_states.empty()) {
            continue;
        }

        oss << "  subgraph cluster_rule_" << rule_index << " {\n";
        oss << "    style=dashed;\n";
        oss << "    color=gray50;\n";
        oss << "    label=\"rule " << rule_index << ": "
            << compiler::common::EscapeGraphvizLabel(
                   std::string(lexer.rules[rule_index].skip ? "skip "
                                                            : "token ") +
                   lexer.rules[rule_index].name)
            << "\";\n";
        for (std::size_t state_index : cluster_states) {
            emitted_in_cluster[state_index] = true;
            emit_state_node(state_index, "    ");
        }
        oss << "  }\n";
    }

    for (std::size_t i = 0; i < dfa.states.size(); ++i) {
        if (emitted_in_cluster[i]) {
            continue;
        }
        emit_state_node(i, "  ");
    }

    for (std::size_t i = 0; i < dfa.states.size(); ++i) {
        std::unordered_map<std::size_t, std::vector<unsigned>> groups;
        for (unsigned byte = 0; byte < 256; ++byte) {
            const std::size_t target = dfa.states[i].transitions[byte];
            if (target == regex::kInvalidDFAState ||
                target >= dfa.states.size()) {
                continue;
            }
            groups[target].push_back(byte);
        }

        for (const auto &[target, bytes] : groups) {
            std::string label;
            for (std::size_t j = 0; j < bytes.size(); ++j) {
                if (j > 0) {
                    label += ",";
                }
                label += EscapeDebugChar(static_cast<char>(bytes[j]));
            }
            oss << "  s" << i << " -> s" << target << " [label=\""
                << compiler::common::EscapeGraphvizLabel(label) << "\"];\n";
        }
    }

    oss << "}\n";
    return oss.str();
}

GeneratedLexerFiles GenerateCppLexer(const CompiledLexer &lexer,
                                     std::string_view header_filename,
                                     std::string_view source_filename) {
    const std::string lexer_name =
        compiler::common::SanitizeIdentifier(lexer.spec.lexer_name, "GeneratedLexer");
    const std::string token_enum_name =
        compiler::common::SanitizeIdentifier(lexer.spec.token_enum_name, "TokenKind");

    GeneratedLexerFiles files;
    files.header_filename = header_filename.empty()
                                ? (lexer_name + ".h")
                                : std::string(header_filename);
    files.source_filename = source_filename.empty()
                                ? (lexer_name + ".cpp")
                                : std::string(source_filename);

    std::vector<std::size_t> emitting_rule_indices;
    std::unordered_map<std::size_t, std::size_t> rule_to_token_kind_index;
    for (const auto &rule : lexer.rules) {
        if (!rule.skip) {
            rule_to_token_kind_index.emplace(rule.rule_index,
                                             emitting_rule_indices.size());
            emitting_rule_indices.push_back(rule.rule_index);
        }
    }

    std::ostringstream header;
    header << "#pragma once\n\n";
    header << "#include <cstddef>\n";
    header << "#include <string_view>\n";
    header << "#include <vector>\n\n";
    header << OpenNamespaces(lexer.spec.namespace_parts);
    header << "enum class " << token_enum_name << " {\n";
    for (std::size_t rule_index : emitting_rule_indices) {
        header << "    "
               << compiler::common::SanitizeIdentifier(lexer.rules[rule_index].name, "Token")
               << ",\n";
    }
    header << "    EndOfFile,\n";
    header << "};\n\n";
    header << "struct Token {\n";
    header << "    " << token_enum_name << " kind;\n";
    header << "    std::string_view lexeme;\n";
    header << "    std::size_t offset;\n";
    header << "    std::size_t line;\n";
    header << "    std::size_t column;\n";
    header << "};\n\n";
    header << "class " << lexer_name << " {\n";
    header << "public:\n";
    header << "    explicit " << lexer_name << "(std::string_view input);\n";
    header << "    [[nodiscard]] std::vector<Token> Tokenize() const;\n";
    header << "private:\n";
    header << "    std::string_view input_;\n";
    header << "};\n";
    header << CloseNamespaces(lexer.spec.namespace_parts);
    files.header_source = header.str();

    std::ostringstream source;
    source << "#include \"" << files.header_filename << "\"\n\n";
    source << "#include <array>\n";
    source << "#include <cstdint>\n";
    source << "#include <stdexcept>\n";
    source << "#include <string_view>\n";
    source << "#include <vector>\n\n";
    source << "namespace {\n";
    source << "inline constexpr std::uint32_t kInvalidState = 0xFFFFFFFFu;\n\n";
    source << "struct CombinedDFAState {\n";
    source << "    std::int32_t accepting_rule_index; // -1 if non-accepting\n";
    source << "    std::array<std::uint32_t, 256> transitions;\n";
    source << "};\n\n";
    source << "struct RuleInfo {\n";
    source << "    bool skip;\n";
    source << "    int token_kind_index;\n";
    source << "};\n\n";
    source << "void AdvanceLocation(std::string_view text, std::size_t& line, "
              "std::size_t& column) {\n";
    source << "    for (char c : text) {\n";
    source << "        if (c == '\\n') { ++line; column = 1; } else { "
              "++column; }\n";
    source << "    }\n";
    source << "}\n\n";

    source << "static const CombinedDFAState kCombinedDFAStates[] = {\n";
    for (const auto &state : lexer.combined_dfa.states) {
        const int accept_rule_index =
            (state.accepting_rule_index == kInvalidAcceptRuleIndex)
                ? -1
                : static_cast<int>(state.accepting_rule_index);
        source << "    CombinedDFAState{" << accept_rule_index
               << ", std::array<std::uint32_t, 256>{";
        for (std::size_t byte = 0; byte < 256; ++byte) {
            if (byte > 0) {
                source << ", ";
            }
            const std::size_t next = state.transitions[byte];
            if (next == regex::kInvalidDFAState ||
                next >= lexer.combined_dfa.states.size()) {
                source << "kInvalidState";
            } else {
                source << static_cast<std::uint32_t>(next) << "u";
            }
        }
        source << "}},\n";
    }
    source << "};\n\n";

    source << "static const RuleInfo kRuleInfos[] = {\n";
    for (std::size_t i = 0; i < lexer.rules.size(); ++i) {
        const auto &rule = lexer.rules[i];
        const auto it = rule_to_token_kind_index.find(rule.rule_index);
        const int token_index = (it == rule_to_token_kind_index.end())
                                    ? -1
                                    : static_cast<int>(it->second);
        source << "    RuleInfo{" << (rule.skip ? "true" : "false") << ", "
               << token_index << "},\n";
    }
    source << "};\n\n";
    source << "} // namespace\n\n";

    source << OpenNamespaces(lexer.spec.namespace_parts);
    source << lexer_name << "::" << lexer_name
           << "(std::string_view input) : input_(input) {}\n\n";
    source << "std::vector<Token> " << lexer_name << "::Tokenize() const {\n";
    source << "    std::vector<Token> tokens;\n";
    source << "    std::size_t offset = 0;\n";
    source << "    std::size_t line = 1;\n";
    source << "    std::size_t column = 1;\n";
    source << "    while (offset < input_.size()) {\n";
    source << "        if (" << lexer.combined_dfa.states.size()
           << "u == 0) throw std::runtime_error(\"invalid lexer DFA\");\n";
    source << "        std::uint32_t state = "
           << (lexer.combined_dfa.start_state == regex::kInvalidDFAState
                   ? static_cast<std::uint32_t>(0xFFFFFFFFu)
                   : static_cast<std::uint32_t>(lexer.combined_dfa.start_state))
           << "u;\n";
    source << "        if (state == kInvalidState || state >= "
           << lexer.combined_dfa.states.size()
           << "u) throw std::runtime_error(\"invalid lexer DFA\");\n";
    source << "        std::size_t best_len = static_cast<std::size_t>(-1);\n";
    source << "        int best_rule_index = -1;\n";
    source
        << "        if (kCombinedDFAStates[state].accepting_rule_index >= 0) "
           "{ best_len = 0; best_rule_index = "
           "kCombinedDFAStates[state].accepting_rule_index; }\n";
    source << "        for (std::size_t i = 0; (offset + i) < input_.size(); "
              "++i) {\n";
    source << "            const std::uint32_t next = "
              "kCombinedDFAStates[state].transitions[static_cast<unsigned "
              "char>(input_[offset + i])];\n";
    source << "            if (next == kInvalidState || next >= "
           << lexer.combined_dfa.states.size() << "u) break;\n";
    source << "            state = next;\n";
    source
        << "            if (kCombinedDFAStates[state].accepting_rule_index >= "
           "0) {\n";
    source << "                best_len = i + 1;\n";
    source << "                best_rule_index = "
              "kCombinedDFAStates[state].accepting_rule_index;\n";
    source << "            }\n";
    source << "        }\n";
    source << "        if (best_rule_index < 0 || best_len == "
              "static_cast<std::size_t>(-1) || best_len == 0) {\n";
    source << "            throw std::runtime_error(\"lexing failed: no rule "
              "matched input\");\n";
    source << "        }\n";
    source << "        const std::string_view lexeme = input_.substr(offset, "
              "best_len);\n";
    source << "        const RuleInfo& rule = "
              "kRuleInfos[static_cast<std::size_t>(best_rule_index)];\n";
    source << "        if (!rule.skip) {\n";
    source << "            tokens.push_back(Token{static_cast<"
           << token_enum_name
           << ">(rule.token_kind_index), lexeme, offset, line, column});\n";
    source << "        }\n";
    source << "        AdvanceLocation(lexeme, line, column);\n";
    source << "        offset += best_len;\n";
    source << "    }\n";
    source << "    tokens.push_back(Token{" << token_enum_name
           << "::EndOfFile, std::string_view{}, offset, line, column});\n";
    source << "    return tokens;\n";
    source << "}\n";
    source << CloseNamespaces(lexer.spec.namespace_parts);
    files.implementation_source = source.str();

    return files;
}
} // namespace compiler::lexgen