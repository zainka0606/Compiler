#include "Generator.h"

#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace compiler::lexgen {

namespace {

struct SourcePos {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

std::string QuoteForMessage(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    out += "\"";
    return out;
}

enum class TokenKind {
    Identifier,
    StringLiteral,
    RegexLiteral,
    Equal,
    Semicolon,
    Scope,
    End
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    SourcePos pos;
};

const char* TokenKindName(TokenKind kind) {
    switch (kind) {
        case TokenKind::Identifier:
            return "identifier";
        case TokenKind::StringLiteral:
            return "string literal";
        case TokenKind::RegexLiteral:
            return "regex literal";
        case TokenKind::Equal:
            return "'='";
        case TokenKind::Semicolon:
            return "';'";
        case TokenKind::Scope:
            return "'::'";
        case TokenKind::End:
            return "end of file";
    }
    return "token";
}

class Lexer {
public:
    explicit Lexer(std::string_view input) : input_(input) {}

    std::vector<Token> LexAll() {
        std::vector<Token> tokens;
        while (true) {
            SkipTrivia();
            Token token = NextToken();
            tokens.push_back(token);
            if (token.kind == TokenKind::End) {
                break;
            }
        }
        return tokens;
    }

private:
    std::string_view input_;
    std::size_t index_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    [[nodiscard]] bool AtEnd() const noexcept {
        return index_ >= input_.size();
    }

    [[nodiscard]] char Peek() const noexcept {
        return AtEnd() ? '\0' : input_[index_];
    }

    [[nodiscard]] char PeekNext() const noexcept {
        return (index_ + 1) < input_.size() ? input_[index_ + 1] : '\0';
    }

    [[nodiscard]] SourcePos Pos() const noexcept {
        return SourcePos{index_, line_, column_};
    }

    char Advance() {
        if (AtEnd()) {
            return '\0';
        }
        const char c = input_[index_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    [[noreturn]] void Error(SourcePos pos, const std::string& message) const {
        throw SpecParseException(pos.offset, pos.line, pos.column, message);
    }

    void SkipTrivia() {
        while (!AtEnd()) {
            const char c = Peek();
            if (std::isspace(static_cast<unsigned char>(c))) {
                Advance();
                continue;
            }
            if (c == '#') {
                while (!AtEnd() && Peek() != '\n') {
                    Advance();
                }
                continue;
            }
            break;
        }
    }

    Token NextToken() {
        const SourcePos pos = Pos();
        if (AtEnd()) {
            return Token{TokenKind::End, {}, pos};
        }

        const char c = Peek();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return LexIdentifier();
        }
        if (c == '"') {
            return LexString();
        }
        if (c == '/') {
            return LexRegex();
        }
        if (c == '=') {
            Advance();
            return Token{TokenKind::Equal, "=", pos};
        }
        if (c == ';') {
            Advance();
            return Token{TokenKind::Semicolon, ";", pos};
        }
        if (c == ':' && PeekNext() == ':') {
            Advance();
            Advance();
            return Token{TokenKind::Scope, "::", pos};
        }

        Error(pos, std::string("unexpected character ") + QuoteForMessage(std::string(1, c)));
    }

    Token LexIdentifier() {
        const SourcePos pos = Pos();
        std::string text;
        while (!AtEnd()) {
            const char c = Peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                text.push_back(Advance());
            } else {
                break;
            }
        }
        return Token{TokenKind::Identifier, std::move(text), pos};
    }

    Token LexString() {
        const SourcePos pos = Pos();
        Advance(); // "
        std::string value;

        while (!AtEnd()) {
            const char c = Advance();
            if (c == '"') {
                return Token{TokenKind::StringLiteral, std::move(value), pos};
            }
            if (c == '\\') {
                if (AtEnd()) {
                    Error(pos, "unterminated string literal");
                }
                const char e = Advance();
                switch (e) {
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 'r':
                        value.push_back('\r');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    case '\\':
                        value.push_back('\\');
                        break;
                    case '"':
                        value.push_back('"');
                        break;
                    case '0':
                        value.push_back('\0');
                        break;
                    default:
                        Error(pos, std::string("unsupported string escape \\") + e);
                }
                continue;
            }
            value.push_back(c);
        }

        Error(pos, "unterminated string literal");
    }

    Token LexRegex() {
        const SourcePos pos = Pos();
        Advance(); // /
        std::string value;

        while (!AtEnd()) {
            const char c = Advance();
            if (c == '/') {
                return Token{TokenKind::RegexLiteral, std::move(value), pos};
            }
            if (c == '\\') {
                if (AtEnd()) {
                    Error(pos, "unterminated regex literal");
                }
                value.push_back(c);
                value.push_back(Advance());
                continue;
            }
            value.push_back(c);
        }

        Error(pos, "unterminated regex literal");
    }
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    LexerSpecAST Parse() {
        LexerSpecAST spec;
        bool saw_lexer = false;
        bool saw_namespace = false;
        bool saw_token_enum = false;

        while (!Check(TokenKind::End)) {
            const Token& keyword = Expect(TokenKind::Identifier, "expected declaration");
            if (keyword.text == "lexer") {
                if (saw_lexer) {
                    Error(keyword, "duplicate 'lexer' declaration");
                }
                spec.lexer_name = Expect(TokenKind::Identifier, "expected lexer class name").text;
                Expect(TokenKind::Semicolon, "expected ';' after lexer declaration");
                saw_lexer = true;
                continue;
            }
            if (keyword.text == "namespace") {
                if (saw_namespace) {
                    Error(keyword, "duplicate 'namespace' declaration");
                }
                spec.namespace_parts = ParseQualifiedIdentifier();
                Expect(TokenKind::Semicolon, "expected ';' after namespace declaration");
                saw_namespace = true;
                continue;
            }
            if (keyword.text == "token_enum") {
                if (saw_token_enum) {
                    Error(keyword, "duplicate 'token_enum' declaration");
                }
                spec.token_enum_name = Expect(TokenKind::Identifier, "expected token enum name").text;
                Expect(TokenKind::Semicolon, "expected ';' after token_enum declaration");
                saw_token_enum = true;
                continue;
            }
            if (keyword.text == "let") {
                MacroDefinition macro;
                macro.name = Expect(TokenKind::Identifier, "expected macro name").text;
                Expect(TokenKind::Equal, "expected '=' after macro name");
                macro.pattern = ParsePattern();
                Expect(TokenKind::Semicolon, "expected ';' after macro definition");
                spec.macros.push_back(std::move(macro));
                continue;
            }
            if (keyword.text == "token" || keyword.text == "skip") {
                RuleDefinition rule;
                rule.skip = (keyword.text == "skip");
                rule.name = Expect(TokenKind::Identifier, "expected rule name").text;
                Expect(TokenKind::Equal, "expected '=' after rule name");
                rule.pattern = ParsePattern();
                Expect(TokenKind::Semicolon, "expected ';' after rule definition");
                spec.rules.push_back(std::move(rule));
                continue;
            }

            Error(keyword, "unknown declaration '" + keyword.text + "'");
        }

        if (spec.rules.empty()) {
            Error(Current(), "lexer spec must declare at least one token/skip rule");
        }
        return spec;
    }

private:
    std::vector<Token> tokens_;
    std::size_t index_ = 0;

    [[nodiscard]] const Token& Current() const {
        return tokens_[index_];
    }

    [[nodiscard]] bool Check(TokenKind kind) const {
        return Current().kind == kind;
    }

    const Token& Advance() {
        if (!Check(TokenKind::End)) {
            ++index_;
        }
        return tokens_[index_ - 1];
    }

    [[noreturn]] void Error(const Token& token, const std::string& message) const {
        throw SpecParseException(token.pos.offset, token.pos.line, token.pos.column, message);
    }

    const Token& Expect(TokenKind kind, const std::string& message) {
        if (!Check(kind)) {
            Error(Current(), message + " (found " + std::string(TokenKindName(Current().kind)) + ")");
        }
        return Advance();
    }

    std::vector<std::string> ParseQualifiedIdentifier() {
        std::vector<std::string> parts;
        parts.push_back(Expect(TokenKind::Identifier, "expected namespace identifier").text);
        while (Check(TokenKind::Scope)) {
            Advance();
            parts.push_back(Expect(TokenKind::Identifier, "expected namespace identifier after '::'").text);
        }
        return parts;
    }

    PatternSource ParsePattern() {
        if (Check(TokenKind::RegexLiteral)) {
            PatternSource pattern;
            pattern.kind = PatternSource::Kind::Regex;
            pattern.text = Advance().text;
            return pattern;
        }
        if (Check(TokenKind::StringLiteral)) {
            PatternSource pattern;
            pattern.kind = PatternSource::Kind::StringLiteral;
            pattern.text = Advance().text;
            return pattern;
        }
        Error(Current(), "expected regex literal (/.../) or string literal (\"...\")");
    }
};

std::string PatternDebugText(const PatternSource& pattern) {
    if (pattern.kind == PatternSource::Kind::Regex) {
        return "/" + pattern.text + "/";
    }
    return QuoteForMessage(pattern.text);
}

} // namespace

SpecParseException::SpecParseException(std::size_t offset, std::size_t line, std::size_t column, std::string message)
    : std::runtime_error(std::move(message)), offset_(offset), line_(line), column_(column) {}

std::size_t SpecParseException::offset() const noexcept {
    return offset_;
}

std::size_t SpecParseException::line() const noexcept {
    return line_;
}

std::size_t SpecParseException::column() const noexcept {
    return column_;
}

LexerSpecAST ParseLexerSpec(std::string_view source_text) {
    Lexer lexer(source_text);
    Parser parser(lexer.LexAll());
    return parser.Parse();
}

std::string ToSpecDebugString(const LexerSpecAST& spec) {
    std::ostringstream oss;
    oss << "LexerSpecAST\n";
    oss << "  lexer_name: " << spec.lexer_name << "\n";
    oss << "  token_enum_name: " << spec.token_enum_name << "\n";
    oss << "  namespace: ";
    if (spec.namespace_parts.empty()) {
        oss << "<global>\n";
    } else {
        for (std::size_t i = 0; i < spec.namespace_parts.size(); ++i) {
            if (i > 0) {
                oss << "::";
            }
            oss << spec.namespace_parts[i];
        }
        oss << "\n";
    }

    oss << "  macros (" << spec.macros.size() << ")\n";
    for (const auto& macro : spec.macros) {
        oss << "    let " << macro.name << " = " << PatternDebugText(macro.pattern) << "\n";
    }

    oss << "  rules (" << spec.rules.size() << ")\n";
    for (const auto& rule : spec.rules) {
        oss << "    " << (rule.skip ? "skip " : "token ") << rule.name << " = " << PatternDebugText(rule.pattern) << "\n";
    }

    return oss.str();
}

} // namespace compiler::lexgen
