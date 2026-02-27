#include "ParserGeneratorStage1.h"

#include "Common/Identifier.h"
#include "Common/StringEscape.h"

#include <sstream>
#include <string>

namespace compiler::parsergen1 {

namespace {

std::string MakeRawStringLiteral(std::string_view text) {
    std::string delimiter = "PGSPEC";
    std::size_t suffix = 0;
    while (true) {
        const std::string closing = ")" + delimiter + "\"";
        if (text.find(closing) == std::string_view::npos) {
            break;
        }
        ++suffix;
        delimiter = "PGSPEC_" + std::to_string(suffix);
    }
    return "R\"" + delimiter + "(" + std::string(text) + ")" + delimiter + "\"";
}

} // namespace

GeneratedParserFiles GenerateCppParser(const GrammarSpecAST &spec,
                                       std::string_view grammar_spec_source,
                                       std::string_view header_filename,
                                       std::string_view source_filename) {
    if (spec.grammar_name.empty()) {
        throw BuildException(
            "grammar name is required to generate parser code");
    }

    GeneratedParserFiles out;
    out.namespace_name =
        compiler::common::SanitizeIdentifier(spec.grammar_name, "Generated");
    out.parser_class_name = out.namespace_name + "Parser";
    out.header_filename = header_filename.empty()
                              ? (out.parser_class_name + ".h")
                              : std::string(header_filename);
    out.source_filename = source_filename.empty()
                              ? (out.parser_class_name + ".cpp")
                              : std::string(source_filename);

    const std::string embedded_spec_literal =
        MakeRawStringLiteral(grammar_spec_source);
    const std::string grammar_name_escaped =
        compiler::common::EscapeForCppString(spec.grammar_name);

    {
        std::ostringstream h;
        h << "#pragma once\n\n";
        h << "#include \"ParserGeneratorStage1.h\"\n\n";
        h << "#include <string>\n";
        h << "#include <string_view>\n";
        h << "#include <vector>\n\n";
        h << "namespace generated::" << out.namespace_name << " {\n\n";
        h << "class " << out.parser_class_name << " {\n";
        h << "public:\n";
        h << "    using Token = compiler::parsergen1::GenericToken;\n";
        h << "    using CST = compiler::parsergen1::CST;\n\n";
        h << "    [[nodiscard]] CST Parse(const std::vector<Token>& tokens) "
             "const;\n";
        h << "    [[nodiscard]] static std::string CSTToGraphvizDot(const CST& "
             "cst, std::string_view graph_name = \"cst\");\n";
        h << "    [[nodiscard]] static const "
             "compiler::parsergen1::LR1ParseTable& ParseTable();\n";
        h << "    [[nodiscard]] static std::string_view GrammarName();\n";
        h << "};\n\n";
        h << "} // namespace generated::" << out.namespace_name << "\n";
        out.header_source = h.str();
    }

    {
        std::ostringstream cc;
        cc << "#include \"" << out.header_filename << "\"\n\n";
        cc << "#include <string>\n";
        cc << "#include <string_view>\n";
        cc << "#include <vector>\n\n";
        cc << "namespace generated::" << out.namespace_name << " {\n\n";
        cc << "namespace {\n";
        cc << "constexpr const char* kEmbeddedGrammarSpec = "
           << embedded_spec_literal << ";\n";
        cc << "} // namespace\n\n";

        cc << "const compiler::parsergen1::LR1ParseTable& "
           << out.parser_class_name << "::ParseTable() {\n";
        cc << "    static const compiler::parsergen1::LR1ParseTable table = "
              "[]() {\n";
        cc << "        compiler::parsergen1::LR1ParseTable built = "
              "compiler::parsergen1::BuildLR1ParseTableFromGrammarSpec("
              "kEmbeddedGrammarSpec);\n";
        cc << "        if (!built.conflicts.empty()) {\n";
        cc << "            throw "
              "compiler::parsergen1::BuildException(\"generated parser grammar "
              "has \" + std::to_string(built.conflicts.size()) + \" "
              "conflict(s)\");\n";
        cc << "        }\n";
        cc << "        return built;\n";
        cc << "    }();\n";
        cc << "    return table;\n";
        cc << "}\n\n";

        cc << "std::string_view " << out.parser_class_name
           << "::GrammarName() {\n";
        cc << "    return \"" << grammar_name_escaped << "\";\n";
        cc << "}\n\n";

        cc << out.parser_class_name << "::CST " << out.parser_class_name
           << "::Parse(const std::vector<Token>& tokens) const {\n";
        cc << "    return compiler::parsergen1::ParseTokensToCST(ParseTable(), "
              "tokens);\n";
        cc << "}\n\n";

        cc << "std::string " << out.parser_class_name
           << "::CSTToGraphvizDot(const CST& cst, std::string_view graph_name) "
              "{\n";
        cc << "    return compiler::parsergen1::CSTToGraphvizDot(cst, "
              "graph_name);\n";
        cc << "}\n\n";

        cc << "} // namespace generated::" << out.namespace_name << "\n";
        out.implementation_source = cc.str();
    }

    return out;
}

} // namespace compiler::parsergen1
