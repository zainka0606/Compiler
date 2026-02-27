#include "Parser.h"

#include "GeneratedParser.h"
#include "LanguageLexer.h"

#include <sstream>
#include <string>
#include <vector>

namespace compiler::lang {

namespace {

using GeneratedLexer = detail::LanguageLexer;
using GeneratedToken = detail::Token;
using GeneratedTokenKind = detail::LanguageTokenKind;
using GeneratedParser = generated::MiniLang::MiniLangParser;

const char *TerminalNameForToken(GeneratedTokenKind kind) {
    switch (kind) {
    case GeneratedTokenKind::ID:
        return "ID";
    case GeneratedTokenKind::NUMBER:
        return "NUMBER";
    case GeneratedTokenKind::SYMBOL:
        return "SYMBOL";
    case GeneratedTokenKind::EndOfFile:
        return "$";
    }
    return "<invalid>";
}

std::vector<compiler::parsergen::GenericToken>
ToGenericTokens(const std::vector<GeneratedToken> &tokens) {
    std::vector<compiler::parsergen::GenericToken> out;
    out.reserve(tokens.size());
    for (const GeneratedToken &token : tokens) {
        out.push_back(compiler::parsergen::GenericToken{
            .kind = TerminalNameForToken(token.kind),
            .lexeme = std::string(token.lexeme),
            .line = token.line,
            .column = token.column,
        });
    }
    return out;
}

} // namespace

AST ParseProgram(std::string_view source_text) {
    try {
        GeneratedLexer lexer(source_text);
        const std::vector<GeneratedToken> tokens = lexer.Tokenize();
        const std::vector<compiler::parsergen::GenericToken> parser_tokens =
            ToGenericTokens(tokens);

        GeneratedParser parser;
        return parser.ParseToAST(parser_tokens);
    } catch (const ParserException &) {
        throw;
    } catch (const compiler::parsergen::ParseException &ex) {
        throw ParserException("internal grammar parse error at " +
                              std::to_string(ex.line()) + ":" +
                              std::to_string(ex.column()) + ": " + ex.what());
    } catch (const compiler::parsergen::BuildException &ex) {
        throw ParserException(
            std::string("internal parser generator build error: ") + ex.what());
    } catch (const std::exception &ex) {
        throw ParserException(ex.what());
    }
}

} // namespace compiler::lang
