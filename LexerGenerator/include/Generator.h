#pragma once

#include "DFA.h"
#include "DFAMinimizer.h"
#include "NFA.h"
#include "Parser.h"

#include <cstddef>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace compiler::lexgen {

struct PatternSource {
    enum class Kind {
        Regex,
        StringLiteral
    };

    Kind kind = Kind::Regex;
    std::string text;
};

struct MacroDefinition {
    std::string name;
    PatternSource pattern;
};

struct RuleDefinition {
    std::string name;
    bool skip = false;
    PatternSource pattern;
};

struct LexerSpecAST {
    std::string lexer_name = "GeneratedLexer";
    std::string token_enum_name = "TokenKind";
    std::vector<std::string> namespace_parts;
    std::vector<MacroDefinition> macros;
    std::vector<RuleDefinition> rules;
};

class SpecParseException : public std::runtime_error {
public:
    SpecParseException(std::size_t offset, std::size_t line, std::size_t column, std::string message);

    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t line() const noexcept;
    [[nodiscard]] std::size_t column() const noexcept;

private:
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

class LexerCompileException : public std::runtime_error {
public:
    explicit LexerCompileException(std::string message);
};

class LexerRuntimeException : public std::runtime_error {
public:
    LexerRuntimeException(std::size_t offset, std::size_t line, std::size_t column, std::string message);

    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t line() const noexcept;
    [[nodiscard]] std::size_t column() const noexcept;

private:
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

struct CompiledRule {
    std::string name;
    bool skip = false;
    std::size_t rule_index = 0;
    std::string source_pattern;
    std::string expanded_regex_pattern;
    regex::RegexNode regex_ast;
    regex::NFA nfa;
    regex::DFA dfa;
    regex::DFA minimized_dfa;
};

inline constexpr std::size_t kInvalidAcceptRuleIndex = static_cast<std::size_t>(-1);

struct CombinedDFAState {
    std::array<std::size_t, 256> transitions{};
    std::size_t accepting_rule_index = kInvalidAcceptRuleIndex;
    std::size_t exclusive_rule_index = kInvalidAcceptRuleIndex;
};

struct CombinedDFA {
    std::vector<CombinedDFAState> states;
    std::size_t start_state = regex::kInvalidDFAState;
};

struct CompiledLexer {
    LexerSpecAST spec;
    std::vector<CompiledRule> rules;
    CombinedDFA combined_dfa;
};

struct RuntimeToken {
    std::string kind;
    std::string lexeme;
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

struct GeneratedLexerFiles {
    std::string header_filename;
    std::string source_filename;
    std::string header_source;
    std::string implementation_source;
};

LexerSpecAST ParseLexerSpec(std::string_view source_text);
std::string ToSpecDebugString(const LexerSpecAST& spec);

CompiledLexer CompileLexerSpec(const LexerSpecAST& spec);
CompiledLexer CompileLexerSpec(std::string_view source_text);
std::vector<RuntimeToken> Tokenize(const CompiledLexer& lexer, std::string_view input);

GeneratedLexerFiles GenerateCppLexer(const CompiledLexer& lexer,
                                     std::string_view header_filename = {},
                                     std::string_view source_filename = {});

std::string BuildCompiledASTDot(const CompiledLexer& lexer);
std::string NFAToGraphvizDot(const regex::NFA& nfa, std::string_view graph_name = "nfa");
std::string DFAToGraphvizDot(const regex::DFA& dfa, std::string_view graph_name = "dfa");
std::string CombinedDFAToGraphvizDot(const CompiledLexer& lexer, std::string_view graph_name = "lexer_dfa");

int RunLexerGeneratorCLI(int argc, const char* const* argv);

} // namespace compiler::lexgen
