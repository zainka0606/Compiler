#include "Common/FileIO.h"
#include "ExampleInputLexer.h"
#include "LR1ParserGenerator.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {
using example::lr1input::ExampleInputLexer;
using example::lr1input::ExampleInputTokenKind;
using InputToken = example::lr1input::Token;

using NumberList = std::vector<double>;
using SemanticValue =
    std::variant<std::monostate, double, std::string, NumberList>;

struct EvalContext {
    std::unordered_map<std::string, double> variables;
};

std::string_view TrimAscii(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const char c = text[begin];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin) {
        const char c = text[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        --end;
    }

    return text.substr(begin, end - begin);
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

bool Matches(const compiler::lr1::FlattenedProduction &production,
             std::string_view lhs,
             std::initializer_list<std::string_view> rhs) {
    if (production.lhs != lhs || production.rhs.size() != rhs.size()) {
        return false;
    }
    std::size_t i = 0;
    for (std::string_view symbol : rhs) {
        if (production.rhs[i] != symbol) {
            return false;
        }
        ++i;
    }
    return true;
}

double AsNumber(const SemanticValue &value, std::string_view context) {
    if (const auto *number = std::get_if<double>(&value)) {
        return *number;
    }
    throw std::runtime_error(std::string(context) + ": expected number");
}

const std::string &AsIdentifier(const SemanticValue &value,
                                std::string_view context) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return *text;
    }
    throw std::runtime_error(std::string(context) + ": expected identifier");
}

const NumberList &AsArgs(const SemanticValue &value, std::string_view context) {
    if (const auto *args = std::get_if<NumberList>(&value)) {
        return *args;
    }
    throw std::runtime_error(std::string(context) + ": expected argument list");
}

double ParseNumberLiteral(std::string_view text) {
    std::string copy(text);
    char *end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        throw std::runtime_error("invalid number literal: " + copy);
    }
    return value;
}

double CallBuiltin(std::string_view name, const NumberList &args) {
    auto require_count = [&](std::size_t expected) {
        if (args.size() != expected) {
            throw std::runtime_error("function '" + std::string(name) +
                                     "' expects " + std::to_string(expected) +
                                     " argument(s), got " +
                                     std::to_string(args.size()));
        }
    };
    auto require_min_count = [&](std::size_t minimum) {
        if (args.size() < minimum) {
            throw std::runtime_error(
                "function '" + std::string(name) + "' expects at least " +
                std::to_string(minimum) + " argument(s), got " +
                std::to_string(args.size()));
        }
    };

    if (name == "sin") {
        require_count(1);
        return std::sin(args[0]);
    }
    if (name == "cos") {
        require_count(1);
        return std::cos(args[0]);
    }
    if (name == "tan") {
        require_count(1);
        return std::tan(args[0]);
    }
    if (name == "sqrt") {
        require_count(1);
        return std::sqrt(args[0]);
    }
    if (name == "abs") {
        require_count(1);
        return std::fabs(args[0]);
    }
    if (name == "exp") {
        require_count(1);
        return std::exp(args[0]);
    }
    if (name == "ln") {
        require_count(1);
        return std::log(args[0]);
    }
    if (name == "log10") {
        require_count(1);
        return std::log10(args[0]);
    }
    if (name == "pow") {
        require_count(2);
        return std::pow(args[0], args[1]);
    }
    if (name == "min") {
        require_min_count(1);
        double out = args[0];
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] < out) {
                out = args[i];
            }
        }
        return out;
    }
    if (name == "max") {
        require_min_count(1);
        double out = args[0];
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] > out) {
                out = args[i];
            }
        }
        return out;
    }
    if (name == "sum") {
        require_min_count(1);
        double out = 0.0;
        for (double value : args) {
            out += value;
        }
        return out;
    }

    throw std::runtime_error("unknown function: " + std::string(name));
}

SemanticValue TokenSemanticValue(const InputToken &token) {
    switch (token.kind) {
    case ExampleInputTokenKind::NUMBER:
        return ParseNumberLiteral(token.lexeme);
    case ExampleInputTokenKind::ID:
        return std::string(token.lexeme);
    case ExampleInputTokenKind::PLUS:
    case ExampleInputTokenKind::MINUS:
    case ExampleInputTokenKind::STAR:
    case ExampleInputTokenKind::SLASH:
    case ExampleInputTokenKind::CARET:
    case ExampleInputTokenKind::LPAREN:
    case ExampleInputTokenKind::RPAREN:
    case ExampleInputTokenKind::COMMA:
    case ExampleInputTokenKind::EndOfFile:
        return std::monostate{};
    }
    return std::monostate{};
}

SemanticValue
ReduceSemantic(const compiler::lr1::FlattenedProduction &production,
               const std::vector<SemanticValue> &children,
               EvalContext &context) {
    (void)context;
    if (Matches(production, "Expr", {"Expr", "PLUS", "Term"})) {
        return AsNumber(children[0], "Expr PLUS Term") +
               AsNumber(children[2], "Expr PLUS Term");
    }
    if (Matches(production, "Expr", {"Expr", "MINUS", "Term"})) {
        return AsNumber(children[0], "Expr MINUS Term") -
               AsNumber(children[2], "Expr MINUS Term");
    }
    if (Matches(production, "Expr", {"Term"})) {
        return AsNumber(children[0], "Expr -> Term");
    }

    if (Matches(production, "Term", {"Term", "STAR", "Power"})) {
        return AsNumber(children[0], "Term STAR Power") *
               AsNumber(children[2], "Term STAR Power");
    }
    if (Matches(production, "Term", {"Term", "SLASH", "Power"})) {
        const double divisor = AsNumber(children[2], "Term SLASH Power");
        if (divisor == 0.0) {
            throw std::runtime_error("division by zero");
        }
        return AsNumber(children[0], "Term SLASH Power") / divisor;
    }
    if (Matches(production, "Term", {"Power"})) {
        return AsNumber(children[0], "Term -> Power");
    }

    if (Matches(production, "Power", {"Unary", "CARET", "Power"})) {
        return std::pow(AsNumber(children[0], "Unary CARET Power"),
                        AsNumber(children[2], "Unary CARET Power"));
    }
    if (Matches(production, "Power", {"Unary"})) {
        return AsNumber(children[0], "Power -> Unary");
    }

    if (Matches(production, "Unary", {"MINUS", "Unary"})) {
        return -AsNumber(children[1], "MINUS Unary");
    }
    if (Matches(production, "Unary", {"Primary"})) {
        return AsNumber(children[0], "Unary -> Primary");
    }

    if (Matches(production, "Primary", {"NUMBER"})) {
        return AsNumber(children[0], "Primary -> NUMBER");
    }
    if (Matches(production, "Primary", {"ID"})) {
        const std::string &name = AsIdentifier(children[0], "Primary -> ID");
        const auto it = context.variables.find(name);
        if (it == context.variables.end()) {
            throw std::runtime_error("unknown identifier: " + name);
        }
        return it->second;
    }
    if (Matches(production, "Primary", {"ID", "LPAREN", "ArgList", "RPAREN"})) {
        const std::string &fn_name =
            AsIdentifier(children[0], "Primary -> function call");
        const NumberList &args =
            AsArgs(children[2], "Primary -> function call");
        return CallBuiltin(fn_name, args);
    }
    if (Matches(production, "Primary", {"LPAREN", "Expr", "RPAREN"})) {
        return AsNumber(children[1], "Primary -> parenthesized");
    }

    if (Matches(production, "ArgList", {"Expr"})) {
        return NumberList{AsNumber(children[0], "ArgList -> Expr")};
    }
    if (Matches(production, "ArgList", {"Expr", "COMMA", "ArgList"})) {
        NumberList out;
        out.push_back(AsNumber(children[0], "ArgList -> Expr COMMA ArgList"));
        const NumberList &tail =
            AsArgs(children[2], "ArgList -> Expr COMMA ArgList");
        out.insert(out.end(), tail.begin(), tail.end());
        return out;
    }

    throw std::runtime_error("unsupported production in evaluator: " +
                             production.lhs);
}

double ParseAndEvaluate(const compiler::lr1::LR1ParseTable &table,
                        const std::vector<InputToken> &tokens,
                        EvalContext &context) {
    if (!table.conflicts.empty()) {
        throw std::runtime_error(
            "cannot evaluate with conflicting LR(1) table (" +
            std::to_string(table.conflicts.size()) + " conflict(s))");
    }
    if (table.canonical_collection.states.empty()) {
        throw std::runtime_error("LR(1) table has no states");
    }

    std::vector<std::size_t> state_stack;
    std::vector<SemanticValue> semantic_stack;
    state_stack.push_back(table.canonical_collection.start_state);

    std::size_t token_index = 0;
    std::size_t step_count = 0;

    while (true) {
        if (token_index >= tokens.size()) {
            throw std::runtime_error(
                "token stream ended before parser accepted");
        }
        if (++step_count > 50000) {
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
            semantic_stack.push_back(TokenSemanticValue(token));
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
            if (semantic_stack.size() < pop_count ||
                state_stack.size() < (pop_count + 1)) {
                throw std::runtime_error(
                    "parser stack underflow during reduction");
            }

            std::vector<SemanticValue> children(
                semantic_stack.end() - static_cast<std::ptrdiff_t>(pop_count),
                semantic_stack.end());
            semantic_stack.resize(semantic_stack.size() - pop_count);
            state_stack.resize(state_stack.size() - pop_count);

            SemanticValue reduced =
                ReduceSemantic(production, children, context);

            const std::size_t goto_state_source = state_stack.back();
            const std::size_t *goto_state =
                FindGoto(table, goto_state_source, production.lhs);
            if (goto_state == nullptr) {
                throw std::runtime_error("no GOTO entry for state " +
                                         std::to_string(goto_state_source) +
                                         " and nonterminal '" + production.lhs +
                                         "'");
            }
            semantic_stack.push_back(std::move(reduced));
            state_stack.push_back(*goto_state);
            break;
        }

        case compiler::lr1::LR1ActionKind::Accept: {
            if (token.kind != ExampleInputTokenKind::EndOfFile) {
                throw std::runtime_error("parser accepted before end of input");
            }
            if (semantic_stack.empty()) {
                throw std::runtime_error(
                    "parser accepted with empty semantic stack");
            }
            return AsNumber(semantic_stack.back(), "accept");
        }
        }
    }
}

void PrintHelp() {
    std::cout << "Commands:\n";
    std::cout << "  <expr>      evaluate an expression\n";
    std::cout << "  :help       show help\n";
    std::cout << "  :vars       list predefined constants\n";
    std::cout << "  exit|quit   quit\n";
    std::cout
        << "Builtins: sin cos tan sqrt abs exp ln log10 pow min max sum\n";
}

void PrintVariables(const EvalContext &context) {
    std::cout << "Variables:\n";
    for (const auto &[name, value] : context.variables) {
        std::cout << "  " << name << " = " << std::setprecision(15) << value
                  << "\n";
    }
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2 && argc != 1) {
        std::cerr << "usage: ExampleLR1Calculator [grammar-spec]\n";
        return 2;
    }

    try {
        const std::filesystem::path grammar_path =
            (argc == 2) ? std::filesystem::path(argv[1])
                        : std::filesystem::path("ExampleLR1Calculator.lr1");
        const std::string grammar_text =
            compiler::common::ReadTextFile(grammar_path);
        const compiler::lr1::GrammarSpecAST grammar =
            compiler::lr1::ParseGrammarSpec(grammar_text);
        const compiler::lr1::LR1ParseTable table =
            compiler::lr1::BuildLR1ParseTable(grammar);

        if (!table.conflicts.empty()) {
            std::cerr << "LR(1) grammar has " << table.conflicts.size()
                      << " conflict(s); calculator disabled.\n";
            return 3;
        }

        EvalContext context;
        context.variables["pi"] = std::acos(-1.0);
        context.variables["e"] = std::exp(1.0);
        context.variables["tau"] = 2.0 * std::acos(-1.0);
        context.variables["ans"] = 0.0;

        std::cout << "ExampleLR1Calculator (LR(1) REPL)\n";
        std::cout << "Type :help for commands.\n";

        std::string line;
        while (true) {
            std::cout << "calc> " << std::flush;
            if (!std::getline(std::cin, line)) {
                std::cout << "\n";
                break;
            }

            const std::string_view trimmed = TrimAscii(line);
            if (trimmed.empty()) {
                continue;
            }
            if (trimmed == "exit" || trimmed == "quit") {
                break;
            }
            if (trimmed == ":help") {
                PrintHelp();
                continue;
            }
            if (trimmed == ":vars") {
                PrintVariables(context);
                continue;
            }

            try {
                ExampleInputLexer lexer(trimmed);
                const std::vector<InputToken> tokens = lexer.Tokenize();
                const double value = ParseAndEvaluate(table, tokens, context);
                context.variables["ans"] = value;
                std::cout << "= " << std::setprecision(15) << value << "\n";
            } catch (const std::exception &ex) {
                std::cout << "error: " << ex.what() << "\n";
            }
        }

        return 0;
    } catch (const compiler::lr1::ParseException &ex) {
        std::cerr << "grammar parse error at " << ex.line() << ":"
                  << ex.column() << ": " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}