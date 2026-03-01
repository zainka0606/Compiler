#include "ParserGeneratorStage1.h"

namespace compiler::parsergen1 {

GrammarSpecAST ParseGrammarSpec(const std::string_view source_text) {
    return lr1::ParseGrammarSpec(source_text);
}

LR1CanonicalCollection BuildLR1CanonicalCollection(const GrammarSpecAST &spec) {
    return lr1::BuildLR1CanonicalCollection(spec);
}

LR1ParseTable BuildLR1ParseTable(const GrammarSpecAST &spec) {
    return lr1::BuildLR1ParseTable(spec);
}

LR1ParseTable BuildLR1ParseTableFromGrammarSpec(const std::string_view source_text) {
    const GrammarSpecAST spec =
        ParseGrammarSpec(source_text);
    return parsergen1::BuildLR1ParseTable(spec);
}

std::string GrammarSpecASTToGraphvizDot(const GrammarSpecAST &spec,
                                        const std::string_view graph_name) {
    return lr1::GrammarSpecASTToGraphvizDot(spec, graph_name);
}

std::string
LR1CanonicalCollectionToGraphvizDot(const LR1CanonicalCollection &collection,
                                    const std::string_view graph_name) {
    return lr1::LR1CanonicalCollectionToGraphvizDot(collection,
                                                              graph_name);
}

std::string LR1ParseTableToGraphvizDot(const LR1ParseTable &table,
                                       const std::string_view graph_name) {
    return lr1::LR1ParseTableToGraphvizDot(table, graph_name);
}

} // namespace compiler::parsergen1
