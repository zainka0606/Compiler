#include "Common/Graphviz.h"
#include "Common/Identifier.h"
#include "LR1ParserGenerator.h"

#include "LR1SpecLexer.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace compiler::lr1 {
namespace {
using GeneratedToken = detail::Token;
using GeneratedTokenKind = detail::LR1SpecTokenKind;

const char *TokenKindName(GeneratedTokenKind kind) {
    switch (kind) {
    case GeneratedTokenKind::KW_GRAMMAR:
        return "grammar";
    case GeneratedTokenKind::KW_START:
        return "start";
    case GeneratedTokenKind::KW_TOKEN:
        return "token";
    case GeneratedTokenKind::KW_RULE:
        return "rule";
    case GeneratedTokenKind::ARROW:
        return "->";
    case GeneratedTokenKind::PIPE:
        return "|";
    case GeneratedTokenKind::SEMI:
        return ";";
    case GeneratedTokenKind::IDENT:
        return "identifier";
    case GeneratedTokenKind::EndOfFile:
        return "end of file";
    }
    return "token";
}

std::string SanitizeIdentifier(std::string_view text,
                               std::string_view fallback) {
    return compiler::common::SanitizeIdentifier(text, fallback);
}

class Parser {
  public:
    explicit Parser(std::string_view source_text) {
        try {
            detail::LR1SpecLexer lexer(source_text);
            tokens_ = lexer.Tokenize();
        } catch (const std::exception &ex) {
            throw ParseException(0, 1, 1,
                                 std::string("lexer failed: ") + ex.what());
        }
    }

    GrammarSpecAST ParseFile() {
        GrammarSpecAST spec;
        bool saw_grammar = false;
        bool saw_start = false;
        std::unordered_set<std::string> terminal_names;

        while (!Check(GeneratedTokenKind::EndOfFile)) {
            if (Match(GeneratedTokenKind::KW_GRAMMAR)) {
                if (saw_grammar) {
                    Error(CurrentOrPrevious(), "duplicate grammar declaration");
                }
                spec.grammar_name = ExpectIdentifier("expected grammar name");
                Expect(GeneratedTokenKind::SEMI,
                       "expected ';' after grammar declaration");
                saw_grammar = true;
                continue;
            }

            if (Match(GeneratedTokenKind::KW_START)) {
                if (saw_start) {
                    Error(CurrentOrPrevious(), "duplicate start declaration");
                }
                spec.start_symbol =
                    ExpectIdentifier("expected start symbol name");
                Expect(GeneratedTokenKind::SEMI,
                       "expected ';' after start declaration");
                saw_start = true;
                continue;
            }

            if (Match(GeneratedTokenKind::KW_TOKEN)) {
                const std::string terminal =
                    ExpectIdentifier("expected token name");
                Expect(GeneratedTokenKind::SEMI,
                       "expected ';' after token declaration");
                if (!terminal_names.insert(terminal).second) {
                    Error(CurrentOrPrevious(),
                          "duplicate token declaration: " + terminal);
                }
                spec.terminals.push_back(terminal);
                continue;
            }

            if (Match(GeneratedTokenKind::KW_RULE)) {
                spec.rules.push_back(ParseRule());
                continue;
            }

            Error(Current(),
                  "expected top-level declaration (grammar/start/token/rule)");
        }

        if (!saw_grammar) {
            Error(Current(), "missing required grammar declaration");
        }
        if (!saw_start) {
            Error(Current(), "missing required start declaration");
        }
        if (spec.rules.empty()) {
            Error(Current(), "at least one rule declaration is required");
        }

        bool start_rule_found = false;
        for (const auto &rule : spec.rules) {
            if (rule.lhs == spec.start_symbol) {
                start_rule_found = true;
                break;
            }
        }
        if (!start_rule_found) {
            Error(Current(),
                  "start symbol has no matching rule: " + spec.start_symbol);
        }

        Expect(GeneratedTokenKind::EndOfFile, "expected end of file");
        return spec;
    }

  private:
    std::vector<GeneratedToken> tokens_;
    std::size_t index_ = 0;

    [[nodiscard]] const GeneratedToken &Current() const {
        return tokens_[index_];
    }

    [[nodiscard]] const GeneratedToken &CurrentOrPrevious() const {
        if (index_ == 0) {
            return tokens_.front();
        }
        return tokens_[index_ - 1];
    }

    [[nodiscard]] bool Check(GeneratedTokenKind kind) const {
        return Current().kind == kind;
    }

    bool Match(GeneratedTokenKind kind) {
        if (!Check(kind)) {
            return false;
        }
        Advance();
        return true;
    }

    const GeneratedToken &Advance() {
        const GeneratedToken &token = Current();
        if ((index_ + 1) < tokens_.size()) {
            ++index_;
        }
        return token;
    }

    [[noreturn]] void Error(const GeneratedToken &token,
                            const std::string &message) const {
        throw ParseException(token.offset, token.line, token.column, message);
    }

    const GeneratedToken &Expect(GeneratedTokenKind kind,
                                 const std::string &message) {
        if (!Check(kind)) {
            std::string full = message;
            full += " (found ";
            full += TokenKindName(Current().kind);
            full += ")";
            Error(Current(), full);
        }
        return Advance();
    }

    std::string ExpectIdentifier(const std::string &message) {
        const auto &token = Expect(GeneratedTokenKind::IDENT, message);
        return std::string(token.lexeme);
    }

    ProductionAlternative ParseAlternative() {
        ProductionAlternative alt;

        while (Check(GeneratedTokenKind::IDENT)) {
            alt.symbols.push_back(std::string(Advance().lexeme));
        }

        if (alt.symbols.empty()) {
            Error(Current(), "empty rule alternative is not supported");
        }
        return alt;
    }

    ProductionRule ParseRule() {
        ProductionRule rule;
        rule.lhs = ExpectIdentifier("expected rule left-hand side");
        Expect(GeneratedTokenKind::ARROW, "expected '->' after rule name");

        rule.alternatives.push_back(ParseAlternative());
        while (Match(GeneratedTokenKind::PIPE)) {
            rule.alternatives.push_back(ParseAlternative());
        }

        Expect(GeneratedTokenKind::SEMI, "expected ';' after rule");
        return rule;
    }
};
} // namespace

ParseException::ParseException(std::size_t offset, std::size_t line,
                               std::size_t column, std::string message)
    : std::runtime_error(std::move(message)), offset_(offset), line_(line),
      column_(column) {}

std::size_t ParseException::offset() const noexcept { return offset_; }

std::size_t ParseException::line() const noexcept { return line_; }

std::size_t ParseException::column() const noexcept { return column_; }

GrammarSpecAST ParseGrammarSpec(std::string_view source_text) {
    Parser parser(source_text);
    return parser.ParseFile();
}

std::string ToDebugString(const GrammarSpecAST &spec) {
    std::ostringstream oss;
    oss << "GrammarSpecAST\n";
    oss << "  grammar: " << spec.grammar_name << "\n";
    oss << "  start: " << spec.start_symbol << "\n";
    oss << "  terminals (" << spec.terminals.size() << ")\n";
    for (const auto &terminal : spec.terminals) {
        oss << "    token " << terminal << "\n";
    }
    oss << "  rules (" << spec.rules.size() << ")\n";
    for (const auto &rule : spec.rules) {
        oss << "    rule " << rule.lhs << " -> ";
        for (std::size_t alt_index = 0; alt_index < rule.alternatives.size();
             ++alt_index) {
            if (alt_index > 0) {
                oss << " | ";
            }
            const auto &alt = rule.alternatives[alt_index];
            for (std::size_t symbol_index = 0;
                 symbol_index < alt.symbols.size(); ++symbol_index) {
                if (symbol_index > 0) {
                    oss << ' ';
                }
                oss << alt.symbols[symbol_index];
            }
        }
        oss << "\n";
    }
    return oss.str();
}

std::string GrammarSpecASTToGraphvizDot(const GrammarSpecAST &spec,
                                        std::string_view graph_name) {
    std::ostringstream oss;
    oss << "digraph " << SanitizeIdentifier(graph_name, "grammar_ast")
        << " {\n";
    oss << "  rankdir=TB;\n";
    oss << "  node [shape=box];\n";

    std::size_t next_id = 0;
    auto node_name = [&](std::size_t id) { return "n" + std::to_string(id); };
    auto add_node = [&](std::string_view label) -> std::size_t {
        const std::size_t id = next_id++;
        oss << "  " << node_name(id) << " [label=\""
            << compiler::common::EscapeGraphvizLabel(label) << "\"];\n";
        return id;
    };
    auto add_edge = [&](std::size_t from, std::size_t to) {
        oss << "  " << node_name(from) << " -> " << node_name(to) << ";\n";
    };

    auto make_label = [](std::string_view name,
                         std::initializer_list<std::string> props = {}) {
        std::string label(name);
        for (const auto &prop : props) {
            if (!prop.empty()) {
                label += "\n";
                label += prop;
            }
        }
        return label;
    };

    const std::size_t root = add_node("GrammarSpecAST");
    add_edge(root,
             add_node(make_label("Grammar", {"name=" + spec.grammar_name})));
    add_edge(root,
             add_node(make_label("Start", {"symbol=" + spec.start_symbol})));

    const std::size_t tokens_node = add_node("Tokens");
    add_edge(root, tokens_node);
    for (std::size_t i = 0; i < spec.terminals.size(); ++i) {
        add_edge(tokens_node,
                 add_node(make_label("Token", {"index=" + std::to_string(i),
                                               "name=" + spec.terminals[i]})));
    }

    const std::size_t rules_node = add_node("Rules");
    add_edge(root, rules_node);
    for (std::size_t rule_index = 0; rule_index < spec.rules.size();
         ++rule_index) {
        const auto &rule = spec.rules[rule_index];
        const std::size_t rule_node =
            add_node(make_label("Rule", {"index=" + std::to_string(rule_index),
                                         "lhs=" + rule.lhs}));
        add_edge(rules_node, rule_node);
        for (std::size_t alt_index = 0; alt_index < rule.alternatives.size();
             ++alt_index) {
            const auto &alt = rule.alternatives[alt_index];
            const std::size_t alt_node = add_node(make_label(
                "Alternative", {"index=" + std::to_string(alt_index)}));
            add_edge(rule_node, alt_node);
            for (std::size_t symbol_index = 0;
                 symbol_index < alt.symbols.size(); ++symbol_index) {
                add_edge(alt_node,
                         add_node(make_label(
                             "Symbol", {"index=" + std::to_string(symbol_index),
                                        "name=" + alt.symbols[symbol_index]})));
            }
        }
    }

    oss << "}\n";
    return oss.str();
}
} // namespace compiler::lr1