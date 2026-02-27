#include "ParserGeneratorStage1.h"

#include "Common/Graphviz.h"

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace compiler::parsergen1 {

namespace {

const LR1Action *FindAction(const LR1ParseTable &table, std::size_t state_index,
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

const std::size_t *FindGoto(const LR1ParseTable &table, std::size_t state_index,
                            std::string_view symbol) {
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

std::size_t EmitGraphvizNode(const CSTNode &node, std::ostringstream &out,
                             std::size_t &next_id) {
    const std::size_t id = next_id++;

    std::string label(node.Symbol());
    if (node.IsTerminal() && !node.Lexeme().empty()) {
        label.push_back('\n');
        label += std::string(node.Lexeme());
    }
    out << "  n" << id << " [label=\""
        << compiler::common::EscapeGraphvizLabel(label) << "\"];\n";

    for (const std::unique_ptr<CSTNode> &child : node.Children()) {
        const std::size_t child_id = EmitGraphvizNode(*child, out, next_id);
        out << "  n" << id << " -> n" << child_id << ";\n";
    }

    return id;
}

} // namespace

std::string_view CSTNode::Lexeme() const { return {}; }

const std::vector<std::unique_ptr<CSTNode>> &CSTNode::Children() const {
    static const std::vector<std::unique_ptr<CSTNode>> kEmpty;
    return kEmpty;
}

std::size_t CSTNode::ProductionIndex() const {
    return compiler::lr1::kInvalidIndex;
}

std::size_t CSTNode::SourceRuleIndex() const {
    return compiler::lr1::kInvalidIndex;
}

std::size_t CSTNode::SourceAlternativeIndex() const {
    return compiler::lr1::kInvalidIndex;
}

std::size_t CSTNode::ChildCount() const { return Children().size(); }

const CSTNode &CSTNode::Child(std::size_t index) const {
    const auto &children = Children();
    if (index >= children.size() || children[index] == nullptr) {
        throw std::out_of_range("CST child index out of range");
    }
    return *children[index];
}

CSTTerminalNode::CSTTerminalNode(std::string symbol, std::string lexeme,
                                 std::size_t line, std::size_t column)
    : symbol_(std::move(symbol)), lexeme_(std::move(lexeme)), line_(line),
      column_(column) {}

std::string_view CSTTerminalNode::Symbol() const { return symbol_; }

bool CSTTerminalNode::IsTerminal() const { return true; }

std::string_view CSTTerminalNode::Lexeme() const { return lexeme_; }

std::size_t CSTTerminalNode::Line() const { return line_; }

std::size_t CSTTerminalNode::Column() const { return column_; }

CSTNonterminalNode::CSTNonterminalNode(
    std::string symbol, std::size_t line, std::size_t column,
    std::vector<std::unique_ptr<CSTNode>> children,
    std::size_t production_index, std::size_t source_rule_index,
    std::size_t source_alternative_index)
    : symbol_(std::move(symbol)), line_(line), column_(column),
      children_(std::move(children)), production_index_(production_index),
      source_rule_index_(source_rule_index),
      source_alternative_index_(source_alternative_index) {}

std::string_view CSTNonterminalNode::Symbol() const { return symbol_; }

bool CSTNonterminalNode::IsTerminal() const { return false; }

std::size_t CSTNonterminalNode::Line() const { return line_; }

std::size_t CSTNonterminalNode::Column() const { return column_; }

const std::vector<std::unique_ptr<CSTNode>> &
CSTNonterminalNode::Children() const {
    return children_;
}

std::size_t CSTNonterminalNode::ProductionIndex() const {
    return production_index_;
}

std::size_t CSTNonterminalNode::SourceRuleIndex() const {
    return source_rule_index_;
}

std::size_t CSTNonterminalNode::SourceAlternativeIndex() const {
    return source_alternative_index_;
}

bool CST::Empty() const { return root == nullptr; }

const CSTNode &CST::Root() const {
    if (root == nullptr) {
        throw CSTParseException("CST root is empty");
    }
    return *root;
}

CSTNode &CST::Root() {
    if (root == nullptr) {
        throw CSTParseException("CST root is empty");
    }
    return *root;
}

CST ParseTokensToCST(const LR1ParseTable &table,
                     const std::vector<GenericToken> &tokens) {
    if (!table.conflicts.empty()) {
        throw CSTParseException("cannot parse with conflicting LR(1) table (" +
                                std::to_string(table.conflicts.size()) +
                                " conflict(s))");
    }
    if (table.canonical_collection.states.empty()) {
        throw CSTParseException("LR(1) table has no states");
    }

    CST cst;
    std::vector<std::size_t> state_stack;
    std::vector<std::unique_ptr<CSTNode>> node_stack;
    state_stack.push_back(table.canonical_collection.start_state);

    std::size_t token_index = 0;
    std::size_t step_count = 0;

    while (true) {
        if (token_index >= tokens.size()) {
            throw CSTParseException(
                "token stream ended before parser accepted");
        }
        if (++step_count > 500000) {
            throw CSTParseException("parser exceeded step limit");
        }

        const GenericToken &token = tokens[token_index];
        const std::string_view symbol = token.kind;
        const std::size_t state = state_stack.back();
        const LR1Action *action = FindAction(table, state, symbol);
        if (action == nullptr) {
            std::ostringstream oss;
            oss << "no ACTION entry for state " << state << " and symbol '"
                << symbol << "'";
            if (!token.lexeme.empty()) {
                oss << " (" << token.lexeme << ")";
            }
            oss << " at " << token.line << ":" << token.column;
            throw CSTParseException(oss.str());
        }

        switch (action->kind) {
        case LR1ActionKind::Shift: {
            if (action->target_state == kInvalidIndex) {
                throw CSTParseException("invalid shift target");
            }
            node_stack.push_back(std::make_unique<CSTTerminalNode>(
                token.kind, token.lexeme, token.line, token.column));
            state_stack.push_back(action->target_state);
            ++token_index;
            break;
        }

        case LR1ActionKind::Reduce: {
            if (action->production_index >=
                table.canonical_collection.productions.size()) {
                throw CSTParseException("invalid reduce production index");
            }
            const FlattenedProduction &production =
                table.canonical_collection
                    .productions[action->production_index];
            if (production.is_augmented) {
                throw CSTParseException(
                    "unexpected reduction by augmented production");
            }

            const std::size_t pop_count = production.rhs.size();
            if (node_stack.size() < pop_count ||
                state_stack.size() < (pop_count + 1)) {
                throw CSTParseException(
                    "parser stack underflow during reduction");
            }

            std::vector<std::unique_ptr<CSTNode>> children;
            children.reserve(pop_count);
            auto child_begin =
                node_stack.end() - static_cast<std::ptrdiff_t>(pop_count);
            for (auto it = child_begin; it != node_stack.end(); ++it) {
                children.push_back(std::move(*it));
            }
            node_stack.resize(node_stack.size() - pop_count);
            state_stack.resize(state_stack.size() - pop_count);

            const std::size_t line =
                children.empty() ? token.line : children.front()->Line();
            const std::size_t column =
                children.empty() ? token.column : children.front()->Column();
            node_stack.push_back(std::make_unique<CSTNonterminalNode>(
                production.lhs, line, column, std::move(children),
                action->production_index, production.source_rule_index,
                production.source_alternative_index));

            const std::size_t goto_state_source = state_stack.back();
            const std::size_t *goto_state =
                FindGoto(table, goto_state_source, production.lhs);
            if (goto_state == nullptr) {
                throw CSTParseException("no GOTO entry for state " +
                                        std::to_string(goto_state_source) +
                                        " and nonterminal '" + production.lhs +
                                        "'");
            }

            state_stack.push_back(*goto_state);
            break;
        }

        case LR1ActionKind::Accept: {
            if (token.kind != table.canonical_collection.end_marker) {
                throw CSTParseException("parser accepted before end of input");
            }
            if (node_stack.empty()) {
                throw CSTParseException("parser accepted with empty CST stack");
            }
            cst.root = std::move(node_stack.back());
            node_stack.pop_back();
            return cst;
        }
        }
    }
}

std::string CSTToGraphvizDot(const CST &cst, std::string_view graph_name) {
    if (cst.Empty()) {
        throw CSTParseException("cannot render empty CST");
    }

    std::ostringstream out;
    out << "digraph " << graph_name << " {\n";
    out << "  rankdir=TB;\n";
    out << "  node [shape=box];\n";
    out << "  __root [shape=point];\n";

    std::size_t next_id = 0;
    const std::size_t root_id = EmitGraphvizNode(cst.Root(), out, next_id);
    out << "  __root -> n" << root_id << ";\n";
    out << "}\n";
    return out.str();
}

const FlattenedProduction *
TryGetCSTReductionProduction(const LR1ParseTable &table, const CSTNode &node) {
    if (node.IsTerminal() || node.ProductionIndex() == kInvalidIndex) {
        return nullptr;
    }
    if (node.ProductionIndex() >=
        table.canonical_collection.productions.size()) {
        return nullptr;
    }
    const FlattenedProduction &production =
        table.canonical_collection.productions[node.ProductionIndex()];
    if (production.is_augmented) {
        return nullptr;
    }
    return &production;
}

const FlattenedProduction &GetCSTReductionProduction(const LR1ParseTable &table,
                                                     const CSTNode &node) {
    const FlattenedProduction *production =
        TryGetCSTReductionProduction(table, node);
    if (production == nullptr) {
        throw CSTParseException(
            "CST node is not a valid nonterminal reduction node");
    }
    return *production;
}

bool CSTNodeMatchesProduction(const LR1ParseTable &table, const CSTNode &node,
                              std::string_view lhs,
                              std::initializer_list<std::string_view> rhs) {
    const FlattenedProduction *production =
        TryGetCSTReductionProduction(table, node);
    if (production == nullptr || production->lhs != lhs ||
        production->rhs.size() != rhs.size()) {
        return false;
    }
    std::size_t i = 0;
    for (std::string_view symbol : rhs) {
        if (production->rhs[i] != symbol) {
            return false;
        }
        ++i;
    }
    return true;
}

} // namespace compiler::parsergen1
