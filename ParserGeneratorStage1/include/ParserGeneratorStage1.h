#pragma once

#include "CST.h"

#include <string>
#include <string_view>

namespace compiler::parsergen1 {

using ProductionAlternative = compiler::lr1::ProductionAlternative;
using ProductionRule = compiler::lr1::ProductionRule;
using GrammarSpecAST = compiler::lr1::GrammarSpecAST;
using ParseException = compiler::lr1::ParseException;
using BuildException = compiler::lr1::BuildException;
using FlattenedProduction = compiler::lr1::FlattenedProduction;
using LR1Item = compiler::lr1::LR1Item;
using LR1State = compiler::lr1::LR1State;
using LR1CanonicalCollection = compiler::lr1::LR1CanonicalCollection;
using LR1ActionKind = compiler::lr1::LR1ActionKind;
using LR1Action = compiler::lr1::LR1Action;
using LR1ConflictKind = compiler::lr1::LR1ConflictKind;
using LR1Conflict = compiler::lr1::LR1Conflict;
using LR1ParseTable = compiler::lr1::LR1ParseTable;

inline constexpr std::size_t kInvalidIndex = compiler::lr1::kInvalidIndex;

struct GeneratedParserFiles {
    std::string parser_class_name;
    std::string namespace_name;
    std::string header_filename;
    std::string source_filename;
    std::string header_source;
    std::string implementation_source;
};

GrammarSpecAST ParseGrammarSpec(std::string_view source_text);
LR1CanonicalCollection BuildLR1CanonicalCollection(const GrammarSpecAST& spec);
LR1ParseTable BuildLR1ParseTable(const GrammarSpecAST& spec);
LR1ParseTable BuildLR1ParseTableFromGrammarSpec(std::string_view source_text);

std::string GrammarSpecASTToGraphvizDot(const GrammarSpecAST& spec,
                                        std::string_view graph_name = "grammar_ast");
std::string LR1CanonicalCollectionToGraphvizDot(const LR1CanonicalCollection& collection,
                                                std::string_view graph_name = "lr1_canonical_collection");
std::string LR1ParseTableToGraphvizDot(const LR1ParseTable& table,
                                       std::string_view graph_name = "lr1_parse_table");

GeneratedParserFiles GenerateCppParser(const GrammarSpecAST& spec,
                                       std::string_view grammar_spec_source,
                                       std::string_view header_filename = {},
                                       std::string_view source_filename = {});

int RunParserGeneratorCLI(int argc, const char* const* argv);

} // namespace compiler::parsergen1
