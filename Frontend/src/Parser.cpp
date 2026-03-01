#include "CompilerPipeline.h"

#include "GeneratedParser.h"
#include "NeonLexer.h"

#include <string>
#include <string_view>
#include <vector>

namespace compiler::frontend_pipeline {

namespace {

using GeneratedLexer = interpreter::detail::NeonLexer;
using GeneratedToken = interpreter::detail::Token;
using GeneratedTokenKind = interpreter::detail::NeonTokenKind;
using GeneratedParser = generated::Neon::NeonParser;

const char *TerminalNameForToken(const GeneratedTokenKind kind) {
    switch (kind) {
    case GeneratedTokenKind::KW_FN:
        return "KW_FN";
    case GeneratedTokenKind::KW_CLASS:
        return "KW_CLASS";
    case GeneratedTokenKind::KW_MOD:
        return "KW_MOD";
    case GeneratedTokenKind::KW_USE:
        return "KW_USE";
    case GeneratedTokenKind::KW_LET:
        return "KW_LET";
    case GeneratedTokenKind::KW_RETURN:
        return "KW_RETURN";
    case GeneratedTokenKind::KW_BREAK:
        return "KW_BREAK";
    case GeneratedTokenKind::KW_IF:
        return "KW_IF";
    case GeneratedTokenKind::KW_ELSE:
        return "KW_ELSE";
    case GeneratedTokenKind::KW_FOR:
        return "KW_FOR";
    case GeneratedTokenKind::KW_WHILE:
        return "KW_WHILE";
    case GeneratedTokenKind::KW_SWITCH:
        return "KW_SWITCH";
    case GeneratedTokenKind::KW_CASE:
        return "KW_CASE";
    case GeneratedTokenKind::KW_DEFAULT:
        return "KW_DEFAULT";
    case GeneratedTokenKind::KW_TRUE:
        return "KW_TRUE";
    case GeneratedTokenKind::KW_FALSE:
        return "KW_FALSE";
    case GeneratedTokenKind::KW_AS:
        return "KW_AS";
    case GeneratedTokenKind::KW_NEW:
        return "KW_NEW";
    case GeneratedTokenKind::ID:
        return "ID";
    case GeneratedTokenKind::FSTRING:
        return "FSTRING";
    case GeneratedTokenKind::STRING:
        return "STRING";
    case GeneratedTokenKind::CHAR:
        return "CHAR";
    case GeneratedTokenKind::NUMBER:
        return "NUMBER";
    case GeneratedTokenKind::SHLEQ:
        return "SHLEQ";
    case GeneratedTokenKind::SHREQ:
        return "SHREQ";
    case GeneratedTokenKind::PLUSEQ:
        return "PLUSEQ";
    case GeneratedTokenKind::MINUSEQ:
        return "MINUSEQ";
    case GeneratedTokenKind::STAREQ:
        return "STAREQ";
    case GeneratedTokenKind::SLASHEQ:
        return "SLASHEQ";
    case GeneratedTokenKind::MODEQ:
        return "MODEQ";
    case GeneratedTokenKind::ANDEQ:
        return "ANDEQ";
    case GeneratedTokenKind::OREQ:
        return "OREQ";
    case GeneratedTokenKind::CARETEQ:
        return "CARETEQ";
    case GeneratedTokenKind::EQEQ:
        return "EQEQ";
    case GeneratedTokenKind::NEQ:
        return "NEQ";
    case GeneratedTokenKind::LAND:
        return "LAND";
    case GeneratedTokenKind::LOR:
        return "LOR";
    case GeneratedTokenKind::SHL:
        return "SHL";
    case GeneratedTokenKind::SHR:
        return "SHR";
    case GeneratedTokenKind::LTE:
        return "LTE";
    case GeneratedTokenKind::GTE:
        return "GTE";
    case GeneratedTokenKind::LT:
        return "LT";
    case GeneratedTokenKind::GT:
        return "GT";
    case GeneratedTokenKind::PLUS:
        return "PLUS";
    case GeneratedTokenKind::MINUS:
        return "MINUS";
    case GeneratedTokenKind::POW:
        return "POW";
    case GeneratedTokenKind::STAR:
        return "STAR";
    case GeneratedTokenKind::SLASH:
        return "SLASH";
    case GeneratedTokenKind::IDIV:
        return "IDIV";
    case GeneratedTokenKind::MOD:
        return "MOD";
    case GeneratedTokenKind::AMP:
        return "AMP";
    case GeneratedTokenKind::PIPE:
        return "PIPE";
    case GeneratedTokenKind::BANG:
        return "BANG";
    case GeneratedTokenKind::TILDE:
        return "TILDE";
    case GeneratedTokenKind::CARET:
        return "CARET";
    case GeneratedTokenKind::EQUAL:
        return "EQUAL";
    case GeneratedTokenKind::COMMA:
        return "COMMA";
    case GeneratedTokenKind::SEMI:
        return "SEMI";
    case GeneratedTokenKind::QMARK:
        return "QMARK";
    case GeneratedTokenKind::COLON:
        return "COLON";
    case GeneratedTokenKind::DOT:
        return "DOT";
    case GeneratedTokenKind::LPAREN:
        return "LPAREN";
    case GeneratedTokenKind::RPAREN:
        return "RPAREN";
    case GeneratedTokenKind::LBRACE:
        return "LBRACE";
    case GeneratedTokenKind::RBRACE:
        return "RBRACE";
    case GeneratedTokenKind::LBRACKET:
        return "LBRACKET";
    case GeneratedTokenKind::RBRACKET:
        return "RBRACKET";
    case GeneratedTokenKind::EndOfFile:
        return "$";
    }
    return "<invalid>";
}

std::vector<parsergen::GenericToken>
ToGenericTokens(const std::vector<GeneratedToken> &tokens) {
    std::vector<parsergen::GenericToken> out;
    out.reserve(tokens.size());
    for (const GeneratedToken &token : tokens) {
        out.push_back(parsergen::GenericToken{
            .kind = TerminalNameForToken(token.kind),
            .lexeme = std::string(token.lexeme),
            .line = token.line,
            .column = token.column,
        });
    }
    return out;
}

} // namespace

AST ParseProgram(const std::string_view source_text) {
    try {
        GeneratedLexer lexer(source_text);
        const std::vector<GeneratedToken> tokens = lexer.Tokenize();
        const std::vector<parsergen::GenericToken> parser_tokens =
            ToGenericTokens(tokens);

        GeneratedParser parser;
        return parser.ParseToAST(parser_tokens);
    } catch (const FrontendPipelineException &) {
        throw;
    } catch (const parsergen::ParseException &ex) {
        throw FrontendPipelineException(
            "internal grammar parse error at " + std::to_string(ex.line()) +
            ":" + std::to_string(ex.column()) + ": " + ex.what());
    } catch (const parsergen::BuildException &ex) {
        throw FrontendPipelineException(
            std::string("internal parser generator build error: ") + ex.what());
    } catch (const std::exception &ex) {
        throw FrontendPipelineException(ex.what());
    }
}

} // namespace compiler::frontend_pipeline
