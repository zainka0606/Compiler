#include "Common/FileIO.h"
#include "ExampleInputLexer.h"
#include "GeneratedParser.h"

#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using example::calcinput::ExampleInputLexer;
using example::calcinput::ExampleInputTokenKind;
using InputToken = example::calcinput::Token;
using GeneratedParser = generated::CalculatorExpr::CalculatorExprParser;
using GeneratedExpr = generated::CalculatorExpr::ast::Expr;

std::string_view TrimAscii(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const char c = text[begin];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin) {
        const char c = text[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        --end;
    }

    return text.substr(begin, end - begin);
}

std::string ToNumberLexeme(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

const char* TerminalNameForToken(ExampleInputTokenKind kind) {
    switch (kind) {
        case ExampleInputTokenKind::ID:
            return "ID";
        case ExampleInputTokenKind::NUMBER:
            return "NUMBER";
        case ExampleInputTokenKind::PLUS:
            return "PLUS";
        case ExampleInputTokenKind::MINUS:
            return "MINUS";
        case ExampleInputTokenKind::STAR:
            return "STAR";
        case ExampleInputTokenKind::SLASH:
            return "SLASH";
        case ExampleInputTokenKind::CARET:
            return "CARET";
        case ExampleInputTokenKind::LPAREN:
            return "LPAREN";
        case ExampleInputTokenKind::RPAREN:
            return "RPAREN";
        case ExampleInputTokenKind::COMMA:
            return "COMMA";
        case ExampleInputTokenKind::EndOfFile:
            return "$";
    }
    return "<invalid>";
}

std::vector<compiler::parsergen::GenericToken> ToGenericTokens(const std::vector<InputToken>& tokens, double ans_value) {
    std::vector<compiler::parsergen::GenericToken> out;
    out.reserve(tokens.size());

    const std::string ans_lexeme = ToNumberLexeme(ans_value);
    for (const InputToken& token : tokens) {
        if (token.kind == ExampleInputTokenKind::ID && token.lexeme == "ans") {
            out.push_back(compiler::parsergen::GenericToken{
                .kind = "NUMBER",
                .lexeme = ans_lexeme,
                .line = token.line,
                .column = token.column,
            });
            continue;
        }

        out.push_back(compiler::parsergen::GenericToken{
            .kind = TerminalNameForToken(token.kind),
            .lexeme = std::string(token.lexeme),
            .line = token.line,
            .column = token.column,
        });
    }
    return out;
}

[[nodiscard]] double EvaluateAST(const GeneratedParser::AST& ast) {
    if (ast.Empty()) {
        throw std::runtime_error("cannot evaluate empty AST");
    }
    const auto* expr = dynamic_cast<const GeneratedExpr*>(&ast.Root());
    if (expr == nullptr) {
        throw std::runtime_error("generated AST root is not an Expr");
    }
    return expr->eval();
}

struct ParseResult {
    GeneratedParser::CST cst;
    GeneratedParser::AST ast;
};

ParseResult ParseExpression(std::string_view text, double ans_value) {
    ExampleInputLexer lexer(text);
    const std::vector<InputToken> lexer_tokens = lexer.Tokenize();
    const std::vector<compiler::parsergen::GenericToken> parser_tokens = ToGenericTokens(lexer_tokens, ans_value);

    GeneratedParser parser;
    ParseResult out;
    out.cst = parser.Parse(parser_tokens);
    out.ast = parser.ParseToAST(parser_tokens);
    return out;
}

void PrintHelp() {
    std::cout << "Commands:\n";
    std::cout << "  <expr>      evaluate an expression\n";
    std::cout << "  :help       show help\n";
    std::cout << "  :vars       list predefined constants\n";
    std::cout << "  exit|quit   quit\n";
    std::cout << "Builtins: sin cos tan sqrt abs exp ln log10 pow min max sum\n";
}

void PrintVariables(double ans_value) {
    std::cout << "Variables:\n";
    std::cout << "  pi = " << std::setprecision(15) << 3.14159265358979323846 << "\n";
    std::cout << "  e = " << std::setprecision(15) << 2.71828182845904523536 << "\n";
    std::cout << "  tau = " << std::setprecision(15) << 6.28318530717958647692 << "\n";
    std::cout << "  ans = " << std::setprecision(15) << ans_value << "\n";
}

int RunFileMode(const std::filesystem::path& input_path,
                const std::filesystem::path& cst_dot_path,
                const std::filesystem::path& ast_dot_path) {
    double ans_value = 0.0;

    const std::string source = compiler::common::ReadTextFile(input_path);
    const std::string_view trimmed = TrimAscii(source);
    if (trimmed.empty()) {
        throw std::runtime_error("input is empty");
    }

    const ParseResult parsed = ParseExpression(trimmed, ans_value);
    const double value = EvaluateAST(parsed.ast);
    ans_value = value;

    compiler::common::WriteTextFile(cst_dot_path, GeneratedParser::CSTToGraphvizDot(parsed.cst, "cst"));
    compiler::common::WriteTextFile(ast_dot_path, GeneratedParser::ASTToGraphvizDot(parsed.ast, "ast"));

    std::cout << "Wrote CST DOT to " << cst_dot_path.string() << "\n";
    std::cout << "Wrote AST DOT to " << ast_dot_path.string() << "\n";
    std::cout << "Result: " << std::setprecision(15) << value << "\n";
    return 0;
}

int RunREPL() {
    double ans_value = 0.0;

    std::cout << "ExampleCalculator (ParserGenerator + generated AST virtual methods)\n";
    std::cout << "Type :help for commands.\n";

    std::string line;
    while (true) {
        std::cout << "calc> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }

        const std::string_view trimmed = TrimAscii(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed == "exit" || trimmed == "quit") {
            break;
        }
        if (trimmed == ":help") {
            PrintHelp();
            continue;
        }
        if (trimmed == ":vars") {
            PrintVariables(ans_value);
            continue;
        }

        try {
            const ParseResult parsed = ParseExpression(trimmed, ans_value);
            const double value = EvaluateAST(parsed.ast);
            ans_value = value;
            std::cout << "= " << std::setprecision(15) << value << "\n";
        } catch (const std::exception& ex) {
            std::cout << "error: " << ex.what() << "\n";
        }
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        try {
            return RunREPL();
        } catch (const compiler::parsergen::ParseException& ex) {
            std::cerr << "grammar parse error at " << ex.line() << ":" << ex.column() << ": " << ex.what() << "\n";
        } catch (const compiler::parsergen::BuildException& ex) {
            std::cerr << "parser generator error: " << ex.what() << "\n";
        } catch (const std::exception& ex) {
            std::cerr << "error: " << ex.what() << "\n";
        }
        return 1;
    }

    if (argc != 2 && argc != 3 && argc != 4) {
        std::cerr << "usage: ExampleCalculator [<input> [cst-dot-output] [ast-dot-output]]\n";
        return 2;
    }

    try {
        const std::filesystem::path input_path = argv[1];
        const std::filesystem::path cst_dot_path = (argc >= 3) ? std::filesystem::path(argv[2]) : "CST.dot";
        const std::filesystem::path ast_dot_path = (argc >= 4) ? std::filesystem::path(argv[3]) : "AST.dot";
        return RunFileMode(input_path, cst_dot_path, ast_dot_path);
    } catch (const compiler::parsergen::ParseException& ex) {
        std::cerr << "grammar parse error at " << ex.line() << ":" << ex.column() << ": " << ex.what() << "\n";
    } catch (const compiler::parsergen::BuildException& ex) {
        std::cerr << "parser generator error: " << ex.what() << "\n";
    } catch (const compiler::parsergen::CSTParseException& ex) {
        std::cerr << "CST parse error: " << ex.what() << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}
