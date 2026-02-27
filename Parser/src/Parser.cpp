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
    case GeneratedTokenKind::KW_FN:
        return "KW_FN";
    case GeneratedTokenKind::KW_LET:
        return "KW_LET";
    case GeneratedTokenKind::KW_RETURN:
        return "KW_RETURN";
    case GeneratedTokenKind::ID:
        return "ID";
    case GeneratedTokenKind::NUMBER:
        return "NUMBER";
    case GeneratedTokenKind::PLUS:
        return "PLUS";
    case GeneratedTokenKind::MINUS:
        return "MINUS";
    case GeneratedTokenKind::STAR:
        return "STAR";
    case GeneratedTokenKind::SLASH:
        return "SLASH";
    case GeneratedTokenKind::CARET:
        return "CARET";
    case GeneratedTokenKind::EQUAL:
        return "EQUAL";
    case GeneratedTokenKind::COMMA:
        return "COMMA";
    case GeneratedTokenKind::SEMI:
        return "SEMI";
    case GeneratedTokenKind::LPAREN:
        return "LPAREN";
    case GeneratedTokenKind::RPAREN:
        return "RPAREN";
    case GeneratedTokenKind::LBRACE:
        return "LBRACE";
    case GeneratedTokenKind::RBRACE:
        return "RBRACE";
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
