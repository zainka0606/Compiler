#include "Common/FileIO.h"
#include "ExampleLexer.h"

#include <exception>
#include <iostream>
#include <string>

namespace {
const char *ToString(example::lang::ExampleTokenKind kind) {
    switch (kind) {
    case example::lang::ExampleTokenKind::IF:
        return "IF";
    case example::lang::ExampleTokenKind::IDENT:
        return "IDENT";
    case example::lang::ExampleTokenKind::INT:
        return "INT";
    case example::lang::ExampleTokenKind::PLUS:
        return "PLUS";
    case example::lang::ExampleTokenKind::EndOfFile:
        return "EndOfFile";
    }
    return "<unknown>";
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: ExampleLexerDriver <input>\n";
        return 2;
    }

    std::string input;
    try {
        input = compiler::common::ReadTextFile(argv[1]);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 3;
    }

    example::lang::ExampleLexer lexer(input);
    const auto tokens = lexer.Tokenize();

    for (const auto &token : tokens) {
        std::cout << ToString(token.kind) << "|" << token.lexeme << "|"
                  << token.line << ":" << token.column << "\n";
    }

    return 0;
}