#include "Common/FileIO.h"
#include "Common/Graphviz.h"
#include "ExampleInputLexer.h"
#include "LR1ParserGenerator.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using example::lr1input::ExampleInputLexer;
using example::lr1input::ExampleInputTokenKind;
using InputToken = example::lr1input::Token;

struct ParseTreeNode {
    std::string label;
    std::vector<std::size_t> children;
};

struct ParseTree {
    std::vector<ParseTreeNode> nodes;
    std::size_t root = 0;
};

void WriteTextFile(const std::filesystem::path &path, std::string_view text) {
    compiler::common::WriteTextFile(path, text);
}

std::string ReadTextFile(const std::filesystem::path &path) {
    return compiler::common::ReadTextFile(path);
}

const char *TerminalNameForToken(ExampleInputTokenKind kind) {
    switch (kind) {
    case ExampleInputTokenKind::ID:
        return "ID";
    case ExampleInputTokenKind::NUMBER:
        return "NUMBER";
    case ExampleInputTokenKind::PLUS:
        return "PLUS";
    case ExampleInputTokenKind::MINUS:
        return "MINUS";
    case ExampleInputTokenKind::STAR:
        return "STAR";
    case ExampleInputTokenKind::SLASH:
        return "SLASH";
    case ExampleInputTokenKind::CARET:
        return "CARET";
    case ExampleInputTokenKind::LPAREN:
        return "LPAREN";
    case ExampleInputTokenKind::RPAREN:
        return "RPAREN";
    case ExampleInputTokenKind::COMMA:
        return "COMMA";
    case ExampleInputTokenKind::EndOfFile:
        return "$";
    }
    return "<invalid>";
}

const compiler::lr1::LR1Action *
FindAction(const compiler::lr1::LR1ParseTable &table, std::size_t state_index,
           std::string_view symbol) {
    if (state_index >= table.action_rows.size()) {
        return nullptr;
    }
    for (const auto &[entry_symbol, action] : table.action_rows[state_index]) {
        if (entry_symbol == symbol) {
            return &action;
        }
    }
    return nullptr;
}

const std::size_t *FindGoto(const compiler::lr1::LR1ParseTable &table,
                            std::size_t state_index, std::string_view symbol) {
    if (state_index >= table.goto_rows.size()) {
        return nullptr;
    }
    for (const auto &[entry_symbol, target] : table.goto_rows[state_index]) {
        if (entry_symbol == symbol) {
            return &target;
        }
    }
    return nullptr;
}

std::string TokenLeafLabel(ExampleInputTokenKind kind) {
    return std::string(TerminalNameForToken(kind));
}

ParseTree ParseInputWithLR1(const compiler::lr1::LR1ParseTable &table,
                            const std::vector<InputToken> &tokens) {
    if (!table.conflicts.empty()) {
        throw std::runtime_error("cannot parse with conflicting LR(1) table (" +
                                 std::to_string(table.conflicts.size()) +
                                 " conflict(s))");
    }
    if (table.canonical_collection.states.empty()) {
        throw std::runtime_error("LR(1) table has no states");
    }

    ParseTree tree;
    std::vector<std::size_t> state_stack;
    std::vector<std::size_t> node_stack;
    state_stack.push_back(table.canonical_collection.start_state);

    std::size_t token_index = 0;
    std::size_t step_count = 0;

    while (true) {
        if (token_index >= tokens.size()) {
            throw std::runtime_error(
                "token stream ended before parser accepted");
        }
        if (++step_count > 10000) {
            throw std::runtime_error("parser exceeded step limit");
        }

        const InputToken &token = tokens[token_index];
        const std::string_view symbol = TerminalNameForToken(token.kind);
        const std::size_t state = state_stack.back();
        const compiler::lr1::LR1Action *action =
            FindAction(table, state, symbol);
        if (action == nullptr) {
            throw std::runtime_error("no ACTION entry for state " +
                                     std::to_string(state) + " and symbol '" +
                                     std::string(symbol) + "'");
        }

        switch (action->kind) {
        case compiler::lr1::LR1ActionKind::Shift: {
            if (action->target_state == compiler::lr1::kInvalidIndex) {
                throw std::runtime_error("invalid shift target");
            }
            tree.nodes.push_back(ParseTreeNode{TokenLeafLabel(token.kind), {}});
            node_stack.push_back(tree.nodes.size() - 1);
            state_stack.push_back(action->target_state);
            ++token_index;
            break;
        }

        case compiler::lr1::LR1ActionKind::Reduce: {
            if (action->production_index >=
                table.canonical_collection.productions.size()) {
                throw std::runtime_error("invalid reduce production index");
            }
            const auto &production = table.canonical_collection
                                         .productions[action->production_index];
            if (production.is_augmented) {
                throw std::runtime_error(
                    "unexpected reduction by augmented production");
            }

            const std::size_t pop_count = production.rhs.size();
            if (node_stack.size() < pop_count ||
                state_stack.size() < (pop_count + 1)) {
                throw std::runtime_error(
                    "parser stack underflow during reduction");
            }

            std::vector<std::size_t> children(
                node_stack.end() - static_cast<std::ptrdiff_t>(pop_count),
                node_stack.end());
            node_stack.resize(node_stack.size() - pop_count);
            state_stack.resize(state_stack.size() - pop_count);

            tree.nodes.push_back(
                ParseTreeNode{production.lhs, std::move(children)});
            const std::size_t node_index = tree.nodes.size() - 1;

            const std::size_t goto_state_source = state_stack.back();
            const std::size_t *goto_state =
                FindGoto(table, goto_state_source, production.lhs);
            if (goto_state == nullptr) {
                throw std::runtime_error("no GOTO entry for state " +
                                         std::to_string(goto_state_source) +
                                         " and nonterminal '" + production.lhs +
                                         "'");
            }

            node_stack.push_back(node_index);
            state_stack.push_back(*goto_state);
            break;
        }

        case compiler::lr1::LR1ActionKind::Accept: {
            if (token.kind != ExampleInputTokenKind::EndOfFile) {
                throw std::runtime_error("parser accepted before end of input");
            }
            if (node_stack.empty()) {
                throw std::runtime_error(
                    "parser accepted with empty node stack");
            }
            tree.root = node_stack.back();
            return tree;
        }
        }
    }
}

std::string ParseTreeToGraphvizDot(const ParseTree &tree,
                                   std::string_view graph_name = "parse_ast") {
    std::ostringstream out;
    out << "digraph " << graph_name << " {\n";
    out << "  rankdir=TB;\n";
    out << "  node [shape=box];\n";
    out << "  __root [shape=point];\n";
    out << "  __root -> n" << tree.root << ";\n";

    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        out << "  n" << i << " [label=\""
            << compiler::common::EscapeGraphvizLabel(tree.nodes[i].label)
            << "\"];\n";
    }

    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        for (std::size_t child_index = 0;
             child_index < tree.nodes[i].children.size(); ++child_index) {
            const std::size_t child = tree.nodes[i].children[child_index];
            out << "  n" << i << " -> n" << child << ";\n";
        }
    }

    out << "}\n";
    return out.str();
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: ExampleLR1ParserDriver <grammar-spec> <input> "
                     "[ast-dot-output]\n";
        return 2;
    }

    try {
        const std::filesystem::path output_path =
            (argc == 4) ? std::filesystem::path(argv[3])
                        : std::filesystem::path("AST.dot");
        const std::string grammar_text = ReadTextFile(argv[1]);
        const std::string input_text = ReadTextFile(argv[2]);

        const compiler::lr1::GrammarSpecAST grammar =
            compiler::lr1::ParseGrammarSpec(grammar_text);
        const compiler::lr1::LR1ParseTable table =
            compiler::lr1::BuildLR1ParseTable(grammar);

        ExampleInputLexer lexer(input_text);
        const std::vector<InputToken> tokens = lexer.Tokenize();

        const ParseTree parse_tree = ParseInputWithLR1(table, tokens);
        WriteTextFile(output_path, ParseTreeToGraphvizDot(parse_tree));
        std::cout << "Wrote AST DOT to " << output_path.string() << "\n";
        return 0;
    } catch (const compiler::lr1::ParseException &ex) {
        std::cerr << "grammar parse error at " << ex.line() << ":"
                  << ex.column() << ": " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}