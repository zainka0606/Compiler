#pragma once

#include "ParserGeneratorStage1.h"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace compiler::parsergen {

using ParseException = compiler::parsergen1::ParseException;
using BuildException = compiler::parsergen1::BuildException;
using GrammarSpecAST = compiler::parsergen1::GrammarSpecAST;
using LR1CanonicalCollection = compiler::parsergen1::LR1CanonicalCollection;
using LR1ParseTable = compiler::parsergen1::LR1ParseTable;
using FlattenedProduction = compiler::parsergen1::FlattenedProduction;
using GeneratedParserFiles = compiler::parsergen1::GeneratedParserFiles;
using GenericToken = compiler::parsergen1::GenericToken;
using CST = compiler::parsergen1::CST;
using CSTNode = compiler::parsergen1::CSTNode;
using CSTParseException = compiler::parsergen1::CSTParseException;
using ProductionRule = compiler::parsergen1::ProductionRule;
using ProductionAlternative = compiler::parsergen1::ProductionAlternative;

inline constexpr std::size_t kInvalidIndex =
    compiler::parsergen1::kInvalidIndex;

using compiler::parsergen1::BuildLR1CanonicalCollection;
using compiler::parsergen1::BuildLR1ParseTable;
using compiler::parsergen1::BuildLR1ParseTableFromGrammarSpec;
using compiler::parsergen1::CSTNodeMatchesProduction;
using compiler::parsergen1::CSTToGraphvizDot;
using compiler::parsergen1::GetCSTReductionProduction;
using compiler::parsergen1::GrammarSpecASTToGraphvizDot;
using compiler::parsergen1::ParseGrammarSpec;
using compiler::parsergen1::ParseTokensToCST;
using compiler::parsergen1::TryGetCSTReductionProduction;

struct ASTNodeTypeDecl {
    std::string name;
    std::string base_type_name;
    bool is_abstract = false;
    std::vector<std::string> fields;
    struct FieldDecl {
        std::string name;
        std::string type_name; // empty means legacy/untyped field declaration
        bool is_list = false;
    };
    struct VirtualMethodDecl {
        std::string name;
        std::string return_type_name;
    };
    struct VirtualMethodImplDecl {
        std::string name;
        std::string return_type_name;
        std::string body_cpp;
    };
    std::vector<FieldDecl> field_decls;
    std::vector<VirtualMethodDecl> virtual_methods;
    std::vector<VirtualMethodImplDecl> virtual_method_impls;
};

enum class ActionArgKind {
    ChildAST,
    ChildLexeme,
};

struct ActionArg {
    ActionArgKind kind = ActionArgKind::ChildAST;
    std::size_t rhs_index = 0; // 1-based
};

enum class RuleActionKind {
    None,
    Pass,
    Construct,
    InlineCpp,
};

struct RuleAction {
    RuleActionKind kind = RuleActionKind::None;
    std::string target_node_name;
    std::size_t pass_rhs_index = 0; // 1-based, only for Pass
    std::vector<ActionArg> args;
    std::string cpp_code;
};

struct RuleAlternative {
    std::vector<std::string> symbols;
    RuleAction action;
};

struct RuleDefinition {
    std::string lhs;
    std::vector<RuleAlternative> alternatives;
};

struct LiteralTerminalDecl {
    std::string lexeme;
    std::string terminal_name;
    bool is_explicit = false;
};

struct Stage2SpecAST {
    std::string grammar_name;
    std::string start_symbol;
    std::vector<std::string> terminals;
    std::vector<LiteralTerminalDecl> literal_terminals;
    std::vector<ASTNodeTypeDecl> ast_node_types;
    std::vector<RuleDefinition> rules;
};

struct GeneratedASTNodeTextField {
    std::string name;
    std::string value;
};

class GeneratedASTNode;

struct GeneratedASTNodeChildField {
    std::string name;
    std::unique_ptr<GeneratedASTNode> value;
};

class GeneratedASTNode {
  public:
    virtual ~GeneratedASTNode() = default;

    [[nodiscard]] virtual std::string_view KindName() const = 0;
    [[nodiscard]] virtual const std::vector<GeneratedASTNodeChildField> &
    ChildFields() const = 0;
    [[nodiscard]] virtual const std::vector<GeneratedASTNodeTextField> &
    TextFields() const = 0;

    [[nodiscard]] std::size_t ChildCount() const;
    [[nodiscard]] const GeneratedASTNode &Child(std::size_t index) const;
};

class GeneratedCustomASTNode final : public GeneratedASTNode {
  public:
    GeneratedCustomASTNode(std::string kind_name,
                           std::vector<GeneratedASTNodeChildField> child_fields,
                           std::vector<GeneratedASTNodeTextField> text_fields);

    [[nodiscard]] std::string_view KindName() const override;
    [[nodiscard]] const std::vector<GeneratedASTNodeChildField> &
    ChildFields() const override;
    [[nodiscard]] const std::vector<GeneratedASTNodeTextField> &
    TextFields() const override;

  private:
    std::string kind_name_;
    std::vector<GeneratedASTNodeChildField> child_fields_;
    std::vector<GeneratedASTNodeTextField> text_fields_;
};

struct GeneratedAST {
    std::unique_ptr<GeneratedASTNode> root;

    GeneratedAST() = default;
    GeneratedAST(GeneratedAST &&) noexcept = default;
    GeneratedAST &operator=(GeneratedAST &&) noexcept = default;
    GeneratedAST(const GeneratedAST &) = delete;
    GeneratedAST &operator=(const GeneratedAST &) = delete;

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] const GeneratedASTNode &Root() const;
    [[nodiscard]] GeneratedASTNode &Root();
};

Stage2SpecAST ParseStage2Spec(std::string_view source_text);
void ValidateStage2Spec(const Stage2SpecAST &spec);
GrammarSpecAST ToBaseGrammarSpec(const Stage2SpecAST &spec);
LR1CanonicalCollection BuildLR1CanonicalCollection(const Stage2SpecAST &spec);
LR1ParseTable BuildLR1ParseTable(const Stage2SpecAST &spec);
LR1ParseTable BuildLR1ParseTableFromStage2Spec(std::string_view source_text);

std::string Stage2SpecASTToGraphvizDot(
    const Stage2SpecAST &spec,
    std::string_view graph_name = "parser_generator_stage2_spec_ast");
std::string LR1CanonicalCollectionToGraphvizDot(
    const LR1CanonicalCollection &collection,
    std::string_view graph_name = "lr1_canonical_collection");
std::string
LR1ParseTableToGraphvizDot(const LR1ParseTable &table,
                           std::string_view graph_name = "lr1_parse_table");

GeneratedAST BuildGeneratedASTFromCST(const CST &cst,
                                      const LR1ParseTable &parse_table,
                                      const Stage2SpecAST &spec);
std::string GeneratedASTToGraphvizDot(const GeneratedAST &ast,
                                      std::string_view graph_name = "ast");

GeneratedParserFiles GenerateCppParser(const Stage2SpecAST &spec,
                                       std::string_view stage2_spec_source,
                                       std::string_view header_filename = {},
                                       std::string_view source_filename = {});

int RunParserGeneratorCLI(int argc, const char *const *argv);

// Transitional alias while renaming ParserGeneratorStage2 -> ParserGenerator.
inline int RunParserGeneratorStage2CLI(int argc, const char *const *argv) {
    return RunParserGeneratorCLI(argc, argv);
}

} // namespace compiler::parsergen

namespace compiler {
namespace parsergen2 = parsergen;
}
