#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace compiler::slr {
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

struct SLRItem {
    std::size_t production_index = 0;
    std::size_t dot = 0;
};

struct SLRState {
    std::vector<SLRItem> items;
    std::vector<std::pair<std::string, std::size_t>> transitions;
};

struct SLRCanonicalCollection {
    std::string augmented_start_symbol;
    std::string end_marker = "$";
    std::vector<std::string> terminals;
    std::vector<std::string> nonterminals;
    std::vector<FlattenedProduction> productions;
    std::size_t start_state = 0;
    std::vector<SLRState> states;
};

enum class SLRActionKind { Shift, Reduce, Accept };

struct SLRAction {
    SLRActionKind kind = SLRActionKind::Shift;
    std::size_t target_state = kInvalidIndex;     // Shift
    std::size_t production_index = kInvalidIndex; // Reduce
};

enum class SLRConflictKind { ShiftReduce, ReduceReduce, Other };

struct SLRConflict {
    SLRConflictKind kind = SLRConflictKind::Other;
    std::size_t state_index = 0;
    std::string symbol;
    std::vector<SLRAction> actions;
};

struct SLRParseTable {
    SLRCanonicalCollection canonical_collection;
    std::vector<std::vector<std::pair<std::string, SLRAction>>> action_rows;
    std::vector<std::vector<std::pair<std::string, std::size_t>>> goto_rows;
    std::vector<SLRConflict> conflicts;
};

GrammarSpecAST ParseGrammarSpec(std::string_view source_text);
std::string ToDebugString(const GrammarSpecAST &spec);
SLRCanonicalCollection BuildSLRCanonicalCollection(const GrammarSpecAST &spec);
SLRParseTable BuildSLRParseTable(const GrammarSpecAST &spec);
std::string ToDebugString(const SLRCanonicalCollection &collection);
std::string ToDebugString(const SLRParseTable &table);
std::string
GrammarSpecASTToGraphvizDot(const GrammarSpecAST &spec,
                            std::string_view graph_name = "grammar_ast");
std::string SLRCanonicalCollectionToGraphvizDot(
    const SLRCanonicalCollection &collection,
    std::string_view graph_name = "slr_canonical_collection");
std::string
SLRParseTableToGraphvizDot(const SLRParseTable &table,
                           std::string_view graph_name = "slr_parse_table");

int RunSLRParserGeneratorCLI(int argc, const char *const *argv);
} // namespace compiler::slr