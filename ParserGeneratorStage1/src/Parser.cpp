#include "ParserGeneratorStage1.h"

namespace compiler::parsergen1 {

GrammarSpecAST ParseGrammarSpec(std::string_view source_text) {
    return compiler::lr1::ParseGrammarSpec(source_text);
}

LR1CanonicalCollection BuildLR1CanonicalCollection(const GrammarSpecAST &spec) {
    return compiler::lr1::BuildLR1CanonicalCollection(spec);
}

LR1ParseTable BuildLR1ParseTable(const GrammarSpecAST &spec) {
    return compiler::lr1::BuildLR1ParseTable(spec);
}

LR1ParseTable BuildLR1ParseTableFromGrammarSpec(std::string_view source_text) {
    const GrammarSpecAST spec =
        compiler::parsergen1::ParseGrammarSpec(source_text);
    return compiler::parsergen1::BuildLR1ParseTable(spec);
}

std::string GrammarSpecASTToGraphvizDot(const GrammarSpecAST &spec,
                                        std::string_view graph_name) {
    return compiler::lr1::GrammarSpecASTToGraphvizDot(spec, graph_name);
}

std::string
LR1CanonicalCollectionToGraphvizDot(const LR1CanonicalCollection &collection,
                                    std::string_view graph_name) {
    return compiler::lr1::LR1CanonicalCollectionToGraphvizDot(collection,
                                                              graph_name);
}

std::string LR1ParseTableToGraphvizDot(const LR1ParseTable &table,
                                       std::string_view graph_name) {
    return compiler::lr1::LR1ParseTableToGraphvizDot(table, graph_name);
}

} // namespace compiler::parsergen1
