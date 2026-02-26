#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace compiler::lr1 {
struct ProductionAlternative {
    std::vector<std::string> symbols;
};

struct ProductionRule {
    std::string lhs;
    std::vector<ProductionAlternative> alternatives;
};

struct GrammarSpecAST {
    std::string grammar_name;
    std::string start_symbol;
    std::vector<std::string> terminals;
    std::vector<ProductionRule> rules;
};

class ParseException : public std::runtime_error {
  public:
    ParseException(std::size_t offset, std::size_t line, std::size_t column,
                   std::string message);

    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t line() const noexcept;
    [[nodiscard]] std::size_t column() const noexcept;

  private:
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

class BuildException : public std::runtime_error {
  public:
    explicit BuildException(std::string message);
};

inline constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

struct FlattenedProduction {
    std::string lhs;
    std::vector<std::string> rhs;
    bool is_augmented = false;
    std::size_t source_rule_index = kInvalidIndex;
    std::size_t source_alternative_index = kInvalidIndex;
};

struct LR1Item {
    std::size_t production_index = 0;
    std::size_t dot = 0;
    std::string lookahead;
};

struct LR1State {
    std::vector<LR1Item> items;
    std::vector<std::pair<std::string, std::size_t>> transitions;
};

struct LR1CanonicalCollection {
    std::string augmented_start_symbol;
    std::string end_marker = "$";
    std::vector<std::string> terminals;
    std::vector<std::string> nonterminals;
    std::vector<FlattenedProduction> productions;
    std::size_t start_state = 0;
    std::vector<LR1State> states;
};

enum class LR1ActionKind { Shift, Reduce, Accept };

struct LR1Action {
    LR1ActionKind kind = LR1ActionKind::Shift;
    std::size_t target_state = kInvalidIndex;     // Shift
    std::size_t production_index = kInvalidIndex; // Reduce
};

enum class LR1ConflictKind { ShiftReduce, ReduceReduce, Other };

struct LR1Conflict {
    LR1ConflictKind kind = LR1ConflictKind::Other;
    std::size_t state_index = 0;
    std::string symbol;
    std::vector<LR1Action> actions;
};

struct LR1ParseTable {
    LR1CanonicalCollection canonical_collection;
    std::vector<std::vector<std::pair<std::string, LR1Action>>> action_rows;
    std::vector<std::vector<std::pair<std::string, std::size_t>>> goto_rows;
    std::vector<LR1Conflict> conflicts;
};

GrammarSpecAST ParseGrammarSpec(std::string_view source_text);
std::string ToDebugString(const GrammarSpecAST &spec);
LR1CanonicalCollection BuildLR1CanonicalCollection(const GrammarSpecAST &spec);
LR1ParseTable BuildLR1ParseTable(const GrammarSpecAST &spec);
std::string ToDebugString(const LR1CanonicalCollection &collection);
std::string ToDebugString(const LR1ParseTable &table);
std::string
GrammarSpecASTToGraphvizDot(const GrammarSpecAST &spec,
                            std::string_view graph_name = "grammar_ast");
std::string LR1CanonicalCollectionToGraphvizDot(
    const LR1CanonicalCollection &collection,
    std::string_view graph_name = "lr1_canonical_collection");
std::string
LR1ParseTableToGraphvizDot(const LR1ParseTable &table,
                           std::string_view graph_name = "lr1_parse_table");

int RunLR1ParserGeneratorCLI(int argc, const char *const *argv);
} // namespace compiler::lr1